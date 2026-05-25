#include "u273/reference/state_space/U273NetlistLoader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

namespace u273::reference::state_space {

namespace {

[[nodiscard]] std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream input {path, std::ios::binary};
    if (!input) {
        return {};
    }

    std::ostringstream stream {};
    stream << input.rdbuf();
    return stream.str();
}

[[nodiscard]] std::size_t findJsonKey(std::string_view text, std::string_view key, std::size_t offset = 0)
{
    std::string needle {};
    needle.reserve(key.size() + 2);
    needle.push_back('"');
    needle.append(key);
    needle.push_back('"');
    return text.find(needle, offset);
}

[[nodiscard]] std::optional<std::string> extractString(std::string_view text, std::string_view key)
{
    const auto keyPos = findJsonKey(text, key);
    if (keyPos == std::string_view::npos) {
        return std::nullopt;
    }

    auto colon = text.find(':', keyPos);
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    ++colon;
    while (colon < text.size() && std::isspace(static_cast<unsigned char>(text[colon]))) {
        ++colon;
    }
    if (colon >= text.size() || text[colon] != '"') {
        return std::nullopt;
    }

    ++colon;
    std::string value {};
    auto escaped = false;
    for (auto index = colon; index < text.size(); ++index) {
        const auto ch = text[index];
        if (escaped) {
            value.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            return value;
        }
        value.push_back(ch);
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<double> extractNumber(std::string_view text, std::string_view key)
{
    const auto keyPos = findJsonKey(text, key);
    if (keyPos == std::string_view::npos) {
        return std::nullopt;
    }

    auto colon = text.find(':', keyPos);
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    ++colon;
    while (colon < text.size() && std::isspace(static_cast<unsigned char>(text[colon]))) {
        ++colon;
    }

    const auto* begin = text.data() + colon;
    char* end {};
    const auto value = std::strtod(begin, &end);
    if (end == begin || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] bool contains(std::string_view haystack, std::string_view needle) noexcept
{
    return haystack.find(needle) != std::string_view::npos;
}

[[nodiscard]] std::optional<std::string_view> extractObject(std::string_view text, std::string_view key)
{
    const auto keyPos = findJsonKey(text, key);
    if (keyPos == std::string_view::npos) {
        return std::nullopt;
    }

    auto colon = text.find(':', keyPos);
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    ++colon;
    while (colon < text.size() && std::isspace(static_cast<unsigned char>(text[colon]))) {
        ++colon;
    }
    if (colon >= text.size() || text[colon] != '{') {
        return std::nullopt;
    }

    auto inString = false;
    auto escaped = false;
    auto depth = 0;
    const auto objectBegin = colon;
    for (auto index = objectBegin; index < text.size(); ++index) {
        const auto ch = text[index];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                inString = false;
            }
            continue;
        }

        if (ch == '"') {
            inString = true;
            continue;
        }
        if (ch == '{') {
            ++depth;
            continue;
        }
        if (ch == '}') {
            --depth;
            if (depth == 0) {
                return text.substr(objectBegin, index - objectBegin + 1);
            }
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::vector<std::string_view> extractObjectArray(std::string_view text, std::string_view arrayKey)
{
    std::vector<std::string_view> objects {};
    const auto keyPos = findJsonKey(text, arrayKey);
    if (keyPos == std::string_view::npos) {
        return objects;
    }

    const auto arrayBegin = text.find('[', keyPos);
    if (arrayBegin == std::string_view::npos) {
        return objects;
    }

    auto inString = false;
    auto escaped = false;
    auto depth = 0;
    std::size_t objectBegin = std::string_view::npos;

    for (auto index = arrayBegin + 1; index < text.size(); ++index) {
        const auto ch = text[index];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                inString = false;
            }
            continue;
        }

        if (ch == '"') {
            inString = true;
            continue;
        }
        if (ch == '{') {
            if (depth == 0) {
                objectBegin = index;
            }
            ++depth;
            continue;
        }
        if (ch == '}') {
            --depth;
            if (depth == 0 && objectBegin != std::string_view::npos) {
                objects.push_back(text.substr(objectBegin, index - objectBegin + 1));
                objectBegin = std::string_view::npos;
            }
            continue;
        }
        if (ch == ']' && depth == 0) {
            break;
        }
    }

    return objects;
}

// Minimal JSON reader for the generated netlist shape. It deliberately exposes
// only strings, numbers, and top-level component objects used by this loader.
class JsonObjectView {
public:
    explicit JsonObjectView(std::string_view text) noexcept
        : text_(text)
    {
    }

    [[nodiscard]] std::optional<std::string> string(std::string_view key) const
    {
        return extractString(text_, key);
    }

    [[nodiscard]] std::optional<double> number(std::string_view key) const
    {
        return extractNumber(text_, key);
    }

    [[nodiscard]] std::string stringOr(std::string_view key, std::string fallback) const
    {
        return string(key).value_or(std::move(fallback));
    }

    [[nodiscard]] std::optional<JsonObjectView> object(std::string_view key) const
    {
        const auto value = extractObject(text_, key);
        if (!value) {
            return std::nullopt;
        }
        return JsonObjectView {*value};
    }

    [[nodiscard]] std::vector<JsonObjectView> objectArray(std::string_view key) const
    {
        std::vector<JsonObjectView> objects {};
        for (const auto objectText : extractObjectArray(text_, key)) {
            objects.emplace_back(objectText);
        }
        return objects;
    }

private:
    std::string_view text_ {};
};

class NetlistJsonDocument {
public:
    explicit NetlistJsonDocument(std::string text)
        : text_(std::move(text))
    {
    }

    [[nodiscard]] bool isEmpty() const noexcept { return text_.empty(); }

    [[nodiscard]] std::string stringOr(std::string_view key, std::string fallback) const
    {
        return extractString(text_, key).value_or(std::move(fallback));
    }

    [[nodiscard]] std::vector<JsonObjectView> componentObjects() const
    {
        std::vector<JsonObjectView> objects {};
        for (const auto object : extractObjectArray(text_, "components")) {
            objects.emplace_back(object);
        }
        return objects;
    }

    [[nodiscard]] std::optional<JsonObjectView> dcExecution() const
    {
        const auto value = extractObject(text_, "dc_execution");
        if (!value) {
            return std::nullopt;
        }
        return JsonObjectView {*value};
    }

private:
    std::string text_ {};
};

[[nodiscard]] std::pair<std::string, std::string> splitNodePair(std::string_view port)
{
    const auto slash = port.find('/');
    if (slash == std::string_view::npos) {
        return {std::string {port}, "0"};
    }
    return {std::string {port.substr(0, slash)}, std::string {port.substr(slash + 1)}};
}

[[nodiscard]] NodeId nodeFor(CircuitGraph& circuit, std::string_view name)
{
    if (name.empty() || name == "0") {
        return kGroundNode;
    }

    const auto existing = circuit.findNode(name);
    if (existing.value >= 0) {
        return existing;
    }
    return circuit.addNode(std::string {name});
}

[[nodiscard]] DiodeModel diodeModelFor(std::string_view modelName) noexcept
{
    if (contains(modelName, "u273_empirical")) {
        return makeU273EmpiricalCompositeDiode();
    }
    if (contains(modelName, "ZL10") || contains(modelName, "ZL 10")) {
        return makeZl10Approximation();
    }
    if (contains(modelName, "OA154")) {
        return makeOa154Approximation();
    }
    return makeSsd55Approximation();
}

[[nodiscard]] NpnBjtModel bjtModelFor(std::string_view modelName) noexcept
{
    if (contains(modelName, "2N3054")) {
        return makeSmallSignalNpnApproximation(50.0);
    }
    if (contains(modelName, "SST")) {
        return makeSmallSignalNpnApproximation(100.0);
    }
    return makeSmallSignalNpnApproximation(120.0);
}

void markUnresolved(U273NetlistLoadReport& report, std::string id)
{
    ++report.unresolvedComponents;
    report.unresolvedIds.push_back(std::move(id));
}

// Shared mutable state for component handlers. Keeping it explicit prevents
// each handler from knowing about file loading or report finalization.
class NetlistStampingContext {
public:
    NetlistStampingContext(U273LoadedNetlist& loaded, const U273NetlistLoaderOptions& options) noexcept
        : loaded_(loaded)
        , options_(options)
    {
    }

    [[nodiscard]] CircuitGraph& circuit() noexcept { return loaded_.circuit; }
    [[nodiscard]] U273NetlistLoadReport& report() noexcept { return loaded_.report; }
    [[nodiscard]] const U273NetlistLoaderOptions& options() const noexcept { return options_; }

    [[nodiscard]] NodeId nodeFor(std::string_view name)
    {
        return nodeForNodeName(circuit(), name);
    }

    void markUnresolved(std::string id)
    {
        ::u273::reference::state_space::markUnresolved(report(), std::move(id));
    }

private:
    [[nodiscard]] static NodeId nodeForNodeName(CircuitGraph& circuit, std::string_view name)
    {
        return ::u273::reference::state_space::nodeFor(circuit, name);
    }

    U273LoadedNetlist& loaded_;
    const U273NetlistLoaderOptions& options_;
};

[[nodiscard]] std::string componentId(const JsonObjectView& component)
{
    if (const auto id = component.string("id")) {
        return *id;
    }
    return component.string("name").value_or("UNKNOWN_COMPONENT");
}

void stampResistor(const JsonObjectView& component, NetlistStampingContext& context)
{
    const auto id = componentId(component);
    const auto value = component.number("value");
    const auto n1 = component.string("n1");
    const auto n2 = component.string("n2");

    if (value && *value > 0.0 && n1 && n2) {
        context.circuit().addResistor(id, context.nodeFor(*n1), context.nodeFor(*n2), *value);
        ++context.report().stampedResistors;
        return;
    }

    context.markUnresolved(id);
}

void stampCapacitor(const JsonObjectView& component, NetlistStampingContext& context)
{
    const auto id = componentId(component);
    const auto value = component.number("value");
    const auto n1 = component.string("n1");
    const auto n2 = component.string("n2");

    if (value && *value > 0.0 && n1 && n2) {
        context.circuit().addCapacitor(id, context.nodeFor(*n1), context.nodeFor(*n2), *value);
        ++context.report().stampedCapacitors;
        return;
    }

    if (value && *value == 0.0) {
        ++context.report().zeroValueComponents;
        return;
    }

    context.markUnresolved(id);
}

void stampVoltageSource(const JsonObjectView& component, NetlistStampingContext& context)
{
    const auto id = componentId(component);
    const auto value = component.number("value");
    auto positive = component.string("n_plus");
    auto negative = component.string("n_minus");
    if (!positive) {
        positive = component.string("nPlus");
    }
    if (!negative) {
        negative = component.string("nMinus");
    }

    if (value && positive && negative) {
        context.circuit().addVoltageSource(id, context.nodeFor(*positive), context.nodeFor(*negative), *value);
        ++context.report().stampedVoltageSources;
        return;
    }

    context.markUnresolved(id);
}

void stampCurrentSource(const JsonObjectView& component, NetlistStampingContext& context)
{
    const auto id = componentId(component);
    const auto value = component.number("value");
    const auto positive = component.string("n_plus");
    const auto negative = component.string("n_minus");

    if (value && positive && negative) {
        context.circuit().addCurrentSource(id, context.nodeFor(*positive), context.nodeFor(*negative), *value);
        ++context.report().stampedCurrentSources;
        return;
    }

    context.markUnresolved(id);
}

void stampDiode(const JsonObjectView& component, NetlistStampingContext& context)
{
    const auto id = componentId(component);
    const auto anode = component.string("anode");
    const auto cathode = component.string("cathode");
    auto model = component.string("model");
    if (model && !contains(*model, "u273_empirical")) {
        model = std::nullopt;
    }
    if (!model) {
        model = component.string("value");
    }
    if (!model) {
        model = component.string("nominalType");
    }

    if (anode && cathode) {
        context.circuit().addDiode(id, context.nodeFor(*anode), context.nodeFor(*cathode), diodeModelFor(model.value_or("")));
        ++context.report().stampedDiodes;
        return;
    }

    context.markUnresolved(id);
}

void stampPotentiometer(const JsonObjectView& component, NetlistStampingContext& context)
{
    const auto id = componentId(component);
    const auto value = component.number("value");
    const auto end1 = component.string("end1");
    const auto wiper = component.string("wiper");
    const auto end2 = component.string("end2");

    if (value && *value > 0.0 && end1 && wiper && end2) {
        const auto fraction = std::clamp(context.options().potentiometerWiperFraction, 1.0e-6, 1.0 - 1.0e-6);
        context.circuit().addResistor(id + ".A", context.nodeFor(*end1), context.nodeFor(*wiper), *value * fraction);
        context.circuit().addResistor(
            id + ".B",
            context.nodeFor(*wiper),
            context.nodeFor(*end2),
            *value * (1.0 - fraction));
        context.report().stampedResistors += 2;
        ++context.report().potentiometersSplit;
        return;
    }

    context.markUnresolved(id);
}

void stampBjt(const JsonObjectView& component, NetlistStampingContext& context)
{
    ++context.report().guardedActiveDevices;

    const auto collector = component.string("collector");
    const auto base = component.string("base");
    const auto emitter = component.string("emitter");
    const auto model = component.string("value").value_or("");

    if (context.options().bjtStampPolicy == BjtStampPolicy::stampKnownTerminals
        && collector && base && emitter) {
        context.circuit().addNpnBjt(
            componentId(component),
            context.nodeFor(*collector),
            context.nodeFor(*base),
            context.nodeFor(*emitter),
            bjtModelFor(model));
        ++context.report().stampedBjts;
    }
}

void stampTransformerBoundary(const JsonObjectView& component, NetlistStampingContext& context)
{
    const auto id = componentId(component);
    const auto primary = component.string("primary");
    const auto secondary = component.string("secondary");

    if (primary && secondary) {
        const auto [primaryPositive, primaryNegative] = splitNodePair(*primary);
        const auto [secondaryPositive, secondaryNegative] = splitNodePair(*secondary);
        context.circuit().addIdealTransformerPort(
            id,
            context.nodeFor(primaryPositive),
            context.nodeFor(primaryNegative),
            context.nodeFor(secondaryPositive),
            context.nodeFor(secondaryNegative),
            1.0,
            contains(id, "U1") ? 300.0 : 50.0,
            contains(id, "U1") ? 10000.0 : 300.0);
        ++context.report().transformerBoundaries;
        return;
    }

    context.markUnresolved(id);
}

void stampSwitchBoundary(const JsonObjectView&, NetlistStampingContext& context)
{
    ++context.report().switchComponents;
}

using ComponentStampHandler = void (*)(const JsonObjectView&, NetlistStampingContext&);

struct ComponentStampRule {
    std::string_view type {};
    ComponentStampHandler stamp {};
};

[[nodiscard]] constexpr auto componentStampRules() noexcept
{
    // Adding a supported component type should add a handler here, not another
    // branch inside loadFromFile.
    return std::array {
        ComponentStampRule {"resistor", stampResistor},
        ComponentStampRule {"capacitor", stampCapacitor},
        ComponentStampRule {"capacitor_adjust", stampCapacitor},
        ComponentStampRule {"voltage_source", stampVoltageSource},
        ComponentStampRule {"current_source", stampCurrentSource},
        ComponentStampRule {"diode", stampDiode},
        ComponentStampRule {"potentiometer", stampPotentiometer},
        ComponentStampRule {"bjt", stampBjt},
        ComponentStampRule {"transformer", stampTransformerBoundary},
        ComponentStampRule {"switch", stampSwitchBoundary}};
}

void stampComponent(const JsonObjectView& component, NetlistStampingContext& context)
{
    const auto type = component.string("type").value_or("");
    for (const auto& rule : componentStampRules()) {
        if (type == rule.type) {
            rule.stamp(component, context);
            return;
        }
    }

    context.markUnresolved(componentId(component));
}

void stampDcExecutionReference(const JsonObjectView& netlist, U273LoadedNetlist& loaded, const U273NetlistLoaderOptions& options)
{
    NetlistStampingContext context {loaded, options};

    auto objectCount = 0;
    for (const auto& source : netlist.objectArray("voltageSources")) {
        ++objectCount;
        stampVoltageSource(source, context);
    }
    for (const auto& resistor : netlist.objectArray("resistors")) {
        ++objectCount;
        stampResistor(resistor, context);
    }
    for (const auto& diode : netlist.objectArray("diodes")) {
        ++objectCount;
        stampDiode(diode, context);
    }

    loaded.report.componentObjects = objectCount;
    if (objectCount == 0) {
        context.markUnresolved("dc_execution.netlist");
    }
}

void addNumericalNodeGmin(U273LoadedNetlist& loaded, U273NetlistLoadReport& report, double resistanceOhm)
{
    const auto originalNodeCount = loaded.circuit.nodeCount();
    for (auto nodeIndex = 1; nodeIndex < originalNodeCount; ++nodeIndex) {
        const auto node = NodeId {nodeIndex};
        loaded.circuit.addResistor(
            "GMIN." + loaded.circuit.nodeName(node),
            node,
            kGroundNode,
            resistanceOhm);
        ++report.stampedResistors;
        ++report.numericalGminResistors;
    }
}

} // namespace

bool U273NetlistLoadReport::hasUsableCircuit() const noexcept
{
    return componentObjects > 0
        && (stampedResistors + stampedCapacitors + stampedVoltageSources + stampedDiodes) > 0
        && unresolvedComponents < componentObjects;
}

bool U273NetlistLoadReport::isFullActiveClosure() const noexcept
{
    return hasUsableCircuit()
        && guardedActiveDevices == 0
        && unresolvedComponents == 0
        && stampedBjts > 0;
}

u273::core::ModelBoundary U273NetlistLoadReport::boundary() const noexcept
{
    return isFullActiveClosure()
        ? u273::core::ModelBoundary::fullActiveModelUnverified
        : u273::core::ModelBoundary::passWithGuardedBoundaries;
}

U273LoadedNetlist U273NetlistLoader::loadFromFile(const std::filesystem::path& path,
                                                  const U273NetlistLoaderOptions& options)
{
    U273LoadedNetlist loaded {};
    auto& report = loaded.report;
    report.sourcePath = path.string();

    const NetlistJsonDocument document {readTextFile(path)};
    if (document.isEmpty()) {
        report.status = "FILE_NOT_READ";
        markUnresolved(report, "u273_netlist.json");
        return loaded;
    }

    report.status = document.stringOr("status", "UNKNOWN");
    report.scientificBoundary = document.stringOr("scientific_boundary", "");

    if (options.source == U273NetlistSource::dcExecutionReference) {
        const auto dcExecution = document.dcExecution();
        if (!dcExecution) {
            report.status = "DC_EXECUTION_NOT_FOUND";
            markUnresolved(report, "dc_execution");
            return loaded;
        }

        report.status = dcExecution->stringOr("status", "DC_EXECUTION_STATUS_UNKNOWN");
        const auto netlist = dcExecution->object("netlist");
        if (!netlist) {
            report.status = "DC_EXECUTION_NETLIST_NOT_FOUND";
            markUnresolved(report, "dc_execution.netlist");
            return loaded;
        }
        stampDcExecutionReference(*netlist, loaded, options);
    } else {
        const auto componentObjects = document.componentObjects();
        report.componentObjects = static_cast<int>(componentObjects.size());

        NetlistStampingContext context {loaded, options};
        for (const auto& component : componentObjects) {
            stampComponent(component, context);
        }
    }

    if (options.addNumericalNodeGmin && options.nodeGminResistanceOhm > 0.0) {
        addNumericalNodeGmin(loaded, report, options.nodeGminResistanceOhm);
    }

    return loaded;
}

} // namespace u273::reference::state_space
