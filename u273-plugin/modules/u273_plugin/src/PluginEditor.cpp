#include "u273/plugin/PluginEditor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <string>
#include <string_view>

#include "u273/core/Constants.h"
#include "u273/core/ParameterIds.h"

namespace u273::plugin {
namespace {

using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
using TextFormatter = std::function<juce::String(double)>;
using TextParser = std::function<double(const juce::String&)>;

constexpr int kEditorWidth = 980;
constexpr int kEditorHeight = 720;
constexpr float kInitialEditorScale = 0.8f;
constexpr int kInitialEditorWidth = 784;
constexpr int kInitialEditorHeight = 576;
constexpr float kKnobStartAngle = juce::MathConstants<float>::pi * 1.22f;
constexpr float kKnobEndAngle = juce::MathConstants<float>::pi * 2.78f;

namespace palette {
constexpr auto background = 0xff070809;
constexpr auto panelTop = 0xff1d2124;
constexpr auto panelBottom = 0xff0b0d0f;
constexpr auto panelEdge = 0xff5b6268;
constexpr auto silk = 0xffeee8d8;
constexpr auto silkMuted = 0xffbdb5a4;
constexpr auto brass = 0xffb39a69;
constexpr auto brassDark = 0xff4a3d28;
constexpr auto meterPaper = 0xffded1b7;
constexpr auto meterInk = 0xff17120d;
constexpr auto redLamp = 0xffef3c32;
constexpr auto blueAccent = 0xff2fa7df;
} // namespace palette

[[nodiscard]] juce::String juceId(std::string_view id)
{
    return juce::String(std::string(id).c_str());
}

[[nodiscard]] juce::Font uiFont(float height, int styleFlags = juce::Font::plain)
{
    return juce::Font("Arial", height, styleFlags);
}

[[nodiscard]] juce::Rectangle<int> designBounds()
{
    return {0, 0, kEditorWidth, kEditorHeight};
}

[[nodiscard]] juce::String formatDb(double value)
{
    return juce::String(value, 1) + " dB";
}

[[nodiscard]] juce::String formatMs(double value)
{
    if (value >= 1000.0) {
        return juce::String(value / 1000.0, 2) + " s";
    }

    return juce::String(value, value < 10.0 ? 2 : 0) + " ms";
}

[[nodiscard]] juce::String formatPercent(double value)
{
    return juce::String(value * 100.0, 0) + "%";
}

[[nodiscard]] juce::String formatScale(double value)
{
    return juce::String(value, 2) + "x";
}

[[nodiscard]] double parseNumber(const juce::String& text, double scale = 1.0)
{
    return text.retainCharacters("-0123456789.").getDoubleValue() * scale;
}

[[nodiscard]] double parsePlain(const juce::String& text)
{
    return parseNumber(text);
}

[[nodiscard]] juce::Point<float> polarPoint(juce::Point<float> centre, float radius, float degrees)
{
    const auto radians = degrees * juce::MathConstants<float>::pi / 180.0f;
    return {
        centre.x + std::cos(radians) * radius,
        centre.y + std::sin(radians) * radius};
}

void drawInsetPanel(juce::Graphics& graphics, juce::Rectangle<int> bounds, float cornerRadius = 10.0f)
{
    const auto area = bounds.toFloat();
    juce::ColourGradient gradient(
        juce::Colour(palette::panelTop), area.getX(), area.getY(),
        juce::Colour(palette::panelBottom), area.getX(), area.getBottom(), false);
    gradient.addColour(0.45, juce::Colour(0xff121619));

    graphics.setGradientFill(gradient);
    graphics.fillRoundedRectangle(area, cornerRadius);

    graphics.setColour(juce::Colour(0xcc000000));
    graphics.drawRoundedRectangle(area.reduced(2.0f), cornerRadius - 2.0f, 2.0f);
    graphics.setColour(juce::Colour(palette::panelEdge).withAlpha(0.75f));
    graphics.drawRoundedRectangle(area.reduced(0.5f), cornerRadius, 1.0f);
}

void drawTexture(juce::Graphics& graphics, juce::Rectangle<int> bounds)
{
    juce::ColourGradient faceSheen(
        juce::Colour(0x0cffffff), static_cast<float>(bounds.getX()), static_cast<float>(bounds.getY()),
        juce::Colour(0x16000000), static_cast<float>(bounds.getRight()), static_cast<float>(bounds.getBottom()), false);
    faceSheen.addColour(0.48, juce::Colours::transparentBlack);
    graphics.setGradientFill(faceSheen);
    graphics.fillRect(bounds);

    graphics.setColour(juce::Colour(0x05000000));
    for (int y = bounds.getY() + 28; y < bounds.getBottom(); y += 58) {
        graphics.drawHorizontalLine(y, static_cast<float>(bounds.getX()), static_cast<float>(bounds.getRight()));
    }

    graphics.setColour(juce::Colour(0x04ffffff));
    for (int x = bounds.getX() + 30; x < bounds.getRight(); x += 46) {
        graphics.drawVerticalLine(x, static_cast<float>(bounds.getY() + 2), static_cast<float>(bounds.getBottom() - 2));
    }
}

void drawFirstPassButtonFace(juce::Graphics& graphics,
                             juce::Rectangle<int> bounds,
                             bool active,
                             bool highlighted,
                             bool pressed)
{
    const auto area = bounds.toFloat().reduced(1.0f);
    const auto radius = 5.0f;

    graphics.setColour(juce::Colour(0x85000000));
    graphics.fillRoundedRectangle(area.translated(0.0f, 2.0f), radius);

    auto top = active ? juce::Colour(0xff243f57) : juce::Colour(0xff24272b);
    auto middle = active ? juce::Colour(0xff182b3c) : juce::Colour(0xff151719);
    auto bottom = active ? juce::Colour(0xff08121a) : juce::Colour(0xff070809);

    if (highlighted) {
        top = top.brighter(0.12f);
        middle = middle.brighter(0.08f);
    }

    if (pressed) {
        top = top.darker(0.22f);
        bottom = bottom.brighter(0.08f);
    }

    juce::ColourGradient body(top, area.getX(), area.getY(), bottom, area.getX(), area.getBottom(), false);
    body.addColour(0.48, middle);
    graphics.setGradientFill(body);
    graphics.fillRoundedRectangle(area, radius);

    graphics.setColour(juce::Colour(0x40ffffff));
    graphics.drawLine(area.getX() + 3.0f, area.getY() + 1.0f, area.getRight() - 3.0f, area.getY() + 1.0f, 1.0f);

    graphics.setColour(juce::Colour(0xbb000000));
    graphics.drawLine(area.getX() + 3.0f, area.getBottom() - 1.0f, area.getRight() - 3.0f, area.getBottom() - 1.0f, 1.0f);

    graphics.setColour(active ? juce::Colour(0xff7fb5d2) : juce::Colour(palette::brass).withAlpha(0.85f));
    graphics.drawRoundedRectangle(area.reduced(0.5f), radius, active ? 1.6f : 1.1f);

    graphics.setColour(active ? juce::Colour(0x227bc8e8) : juce::Colour(0x16ffffff));
    graphics.drawRoundedRectangle(area.reduced(4.0f), radius - 2.0f, 1.0f);
}

class U273LookAndFeel final : public juce::LookAndFeel_V4 {
public:
    U273LookAndFeel()
    {
        setColour(juce::Label::textColourId, juce::Colour(palette::silk));
        setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxTextColourId, juce::Colour(palette::silk));
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff090b0d));
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff40372b));
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff101214));
        setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff64211d));
        setColour(juce::TextButton::textColourOffId, juce::Colour(palette::silk));
        setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    }

    void drawRotarySlider(juce::Graphics& graphics,
                          int x,
                          int y,
                          int width,
                          int height,
                          float sliderPos,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override
    {
        const auto area = juce::Rectangle<float>(
            static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height))
                              .reduced(5.0f, 5.0f);
        const auto size = std::min(area.getWidth(), area.getHeight());
        const auto radius = size * 0.5f;
        const auto centre = area.getCentre();
        const auto outer = juce::Rectangle<float>(size, size).withCentre(centre);
        const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        for (int tick = 0; tick <= 18; ++tick) {
            const auto t = static_cast<float>(tick) / 18.0f;
            const auto tickAngle = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle);
            const auto major = tick % 3 == 0;
            const auto start = centre + juce::Point<float>(
                                            std::cos(tickAngle - juce::MathConstants<float>::halfPi) * (radius + (major ? 2.0f : 5.0f)),
                                            std::sin(tickAngle - juce::MathConstants<float>::halfPi) * (radius + (major ? 2.0f : 5.0f)));
            const auto end = centre + juce::Point<float>(
                                          std::cos(tickAngle - juce::MathConstants<float>::halfPi) * (radius + 7.0f),
                                          std::sin(tickAngle - juce::MathConstants<float>::halfPi) * (radius + 7.0f));

            graphics.setColour(major ? juce::Colour(palette::silk) : juce::Colour(palette::silkMuted).withAlpha(0.62f));
            graphics.drawLine({start, end}, major ? 1.45f : 0.95f);
        }

        graphics.setColour(juce::Colour(0xaa000000));
        graphics.fillEllipse(outer.translated(0.0f, 5.0f).expanded(4.0f));

        const auto skirt = outer.reduced(radius * 0.02f);
        juce::ColourGradient skirtGradient(
            juce::Colour(0xff62686d), skirt.getX(), skirt.getY(),
            juce::Colour(0xff030405), skirt.getX(), skirt.getBottom(), false);
        skirtGradient.addColour(0.18, juce::Colour(0xff2b2f32));
        skirtGradient.addColour(0.62, juce::Colour(0xff08090a));
        graphics.setGradientFill(skirtGradient);
        graphics.fillEllipse(skirt);

        for (int groove = 0; groove < 48; ++groove) {
            const auto grooveAngle = juce::MathConstants<float>::twoPi * static_cast<float>(groove) / 48.0f;
            const auto grooveMajor = groove % 4 == 0;
            const auto innerRadius = radius * (grooveMajor ? 0.73f : 0.77f);
            const auto outerRadius = radius * 0.98f;
            const auto start = centre + juce::Point<float>(
                                            std::cos(grooveAngle) * innerRadius,
                                            std::sin(grooveAngle) * innerRadius);
            const auto end = centre + juce::Point<float>(
                                          std::cos(grooveAngle) * outerRadius,
                                          std::sin(grooveAngle) * outerRadius);

            graphics.setColour(grooveMajor ? juce::Colour(0xff0b0c0d) : juce::Colour(0xff16191b));
            graphics.drawLine({start, end}, grooveMajor ? 1.45f : 1.0f);
        }

        graphics.setColour(juce::Colour(0xff020304));
        graphics.drawEllipse(skirt, 2.0f);
        graphics.setColour(juce::Colour(0x32ffffff));
        graphics.drawEllipse(skirt.reduced(2.5f), 1.0f);

        const auto darkWell = outer.reduced(radius * 0.20f);
        juce::ColourGradient wellGradient(
            juce::Colour(0xff25292c), darkWell.getX(), darkWell.getY(),
            juce::Colour(0xff050607), darkWell.getX(), darkWell.getBottom(), false);
        wellGradient.addColour(0.55, juce::Colour(0xff0b0d0f));
        graphics.setGradientFill(wellGradient);
        graphics.fillEllipse(darkWell);

        graphics.setColour(juce::Colour(0xff030405));
        graphics.drawEllipse(darkWell, 2.4f);

        const auto cap = outer.reduced(radius * 0.38f);
        juce::ColourGradient metal(
            juce::Colour(0xfff4f2ea), cap.getX() + cap.getWidth() * 0.22f, cap.getY(),
            juce::Colour(0xff767d83), cap.getRight(), cap.getBottom(), false);
        metal.addColour(0.24, juce::Colour(0xffc7c7c0));
        metal.addColour(0.50, juce::Colour(0xfffdfbf4));
        metal.addColour(0.78, juce::Colour(0xff9ca1a5));
        graphics.setGradientFill(metal);
        graphics.fillEllipse(cap);

        juce::Path capHighlight;
        const auto highlightArc = cap.reduced(4.0f);
        capHighlight.addCentredArc(highlightArc.getCentreX(), highlightArc.getCentreY(), highlightArc.getWidth() * 0.5f,
                                   highlightArc.getHeight() * 0.5f, 0.0f, juce::MathConstants<float>::pi * 1.08f,
                                   juce::MathConstants<float>::pi * 1.55f, true);
        graphics.setColour(juce::Colour(0x75ffffff));
        graphics.strokePath(capHighlight, juce::PathStrokeType(1.3f));

        juce::Path capShade;
        const auto shadeArc = cap.reduced(1.5f);
        capShade.addCentredArc(shadeArc.getCentreX(), shadeArc.getCentreY(), shadeArc.getWidth() * 0.5f,
                               shadeArc.getHeight() * 0.5f, 0.0f, juce::MathConstants<float>::pi * 0.15f,
                               juce::MathConstants<float>::pi * 0.72f, true);
        graphics.setColour(juce::Colour(0xaa303438));
        graphics.strokePath(capShade, juce::PathStrokeType(2.2f));

        const auto pointerLength = radius * 0.56f;
        const auto pointerStart = centre + juce::Point<float>(
                                               std::cos(angle - juce::MathConstants<float>::halfPi) * (radius * 0.14f),
                                               std::sin(angle - juce::MathConstants<float>::halfPi) * (radius * 0.14f));
        const auto pointerEnd = centre + juce::Point<float>(
                                             std::cos(angle - juce::MathConstants<float>::halfPi) * pointerLength,
                                             std::sin(angle - juce::MathConstants<float>::halfPi) * pointerLength);
        const auto accentBits = static_cast<juce::uint32>(
            static_cast<int>(slider.getProperties().getWithDefault("accentColour", static_cast<int>(palette::silk))));
        const auto pointerColour = juce::Colour(accentBits);

        graphics.setColour(juce::Colour(0x99000000));
        graphics.drawLine({pointerStart.translated(1.2f, 1.2f), pointerEnd.translated(1.2f, 1.2f)}, 5.0f);
        graphics.setColour(juce::Colour(0xff202326));
        graphics.drawLine({pointerStart, pointerEnd}, 4.0f);
        graphics.setColour(pointerColour.withAlpha(0.92f));
        graphics.drawLine({pointerStart, pointerEnd}, 1.7f);

        graphics.setColour(juce::Colour(0x55ffffff));
        graphics.fillEllipse(cap.withSizeKeepingCentre(cap.getWidth() * 0.28f, cap.getHeight() * 0.20f)
                                .translated(-cap.getWidth() * 0.16f, -cap.getHeight() * 0.20f));

        graphics.setColour(juce::Colour(0xff101214));
        graphics.drawEllipse(cap, 1.3f);
    }

    void drawButtonBackground(juce::Graphics& graphics,
                              juce::Button& button,
                              const juce::Colour&,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override
    {
        const auto active = button.getToggleState() || shouldDrawButtonAsDown;
        drawFirstPassButtonFace(graphics, button.getLocalBounds(), active, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
    }

    void drawLabel(juce::Graphics& graphics, juce::Label& label) override
    {
        const auto bounds = label.getLocalBounds().toFloat();
        graphics.setColour(label.findColour(juce::Label::backgroundColourId));
        graphics.fillRoundedRectangle(bounds.reduced(0.5f), 2.0f);
        graphics.setColour(label.findColour(juce::Label::outlineColourId));
        graphics.drawRoundedRectangle(bounds.reduced(0.5f), 2.0f, 1.0f);

        graphics.setColour(label.findColour(juce::Label::textColourId));
        graphics.setFont(label.getFont());
        graphics.drawFittedText(
            label.getText(), label.getBorderSize().subtractedFrom(label.getLocalBounds()), label.getJustificationType(), 1);
    }
};

class ParameterKnob final : public juce::Component {
public:
    ParameterKnob(juce::AudioProcessorValueTreeState& state,
                  std::string_view parameterId,
                  const juce::String& title,
                  TextFormatter formatter,
                  TextParser parser,
                  juce::Colour accent = juce::Colour(palette::silk))
        : parameterId_(juceId(parameterId))
        , formatter_(std::move(formatter))
        , parser_(std::move(parser))
    {
        title_.setText(title, juce::dontSendNotification);
        title_.setJustificationType(juce::Justification::centred);
        title_.setFont(uiFont(14.0f));
        title_.setColour(juce::Label::textColourId, juce::Colour(palette::silk));
        title_.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(title_);

        slider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider_.setRotaryParameters(kKnobStartAngle, kKnobEndAngle, true);
        slider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider_.setScrollWheelEnabled(true);
        slider_.setPopupDisplayEnabled(false, false, nullptr);
        slider_.getProperties().set("accentColour", static_cast<int>(accent.getARGB()));

        if (formatter_) {
            slider_.textFromValueFunction = formatter_;
        }

        if (parser_) {
            slider_.valueFromTextFunction = parser_;
        }

        addAndMakeVisible(slider_);

        value_.setJustificationType(juce::Justification::centred);
        value_.setFont(uiFont(13.0f));
        value_.setColour(juce::Label::backgroundColourId, juce::Colour(0xff070809).withAlpha(0.72f));
        value_.setColour(juce::Label::outlineColourId, juce::Colour(palette::panelEdge).withAlpha(0.32f));
        value_.setColour(juce::Label::textColourId, juce::Colour(palette::silk));
        value_.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(value_);

        slider_.onValueChange = [this] {
            updateValueLabel();
        };

        attachment_ = std::make_unique<SliderAttachment>(state, parameterId_, slider_);
        updateValueLabel();
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        title_.setBounds(bounds.removeFromTop(22));
        bounds.removeFromBottom(2);
        value_.setBounds(bounds.removeFromBottom(23).reduced(14, 0));
        slider_.setBounds(bounds);
    }

private:
    void updateValueLabel()
    {
        value_.setText(formatter_ ? formatter_(slider_.getValue()) : juce::String(slider_.getValue(), 2),
                       juce::dontSendNotification);
    }

    juce::String parameterId_;
    TextFormatter formatter_;
    TextParser parser_;
    juce::Label title_;
    juce::Slider slider_;
    juce::Label value_;
    std::unique_ptr<SliderAttachment> attachment_;
};

class GainReductionMeter final : public juce::Component {
public:
    void setFrame(const u273::core::MeterFrame& frame)
    {
        if (std::abs(frame.gainReductionDb - gainReductionDb_) < 0.03f && frame.sequence == sequence_) {
            return;
        }

        gainReductionDb_ = frame.gainReductionDb;
        sequence_ = frame.sequence;
        repaint();
    }

    void paint(juce::Graphics& graphics) override
    {
        auto frame = getLocalBounds().reduced(8);
        drawInsetPanel(graphics, frame, 9.0f);

        auto glass = frame.reduced(18, 18);
        graphics.setColour(juce::Colour(0xdd000000));
        graphics.fillRoundedRectangle(glass.toFloat().translated(0.0f, 3.0f), 8.0f);

        juce::ColourGradient paper(
            juce::Colour(0xfff0e5ce), static_cast<float>(glass.getX()), static_cast<float>(glass.getY()),
            juce::Colour(palette::meterPaper), static_cast<float>(glass.getX()), static_cast<float>(glass.getBottom()), false);
        paper.addColour(0.58, juce::Colour(0xffcdbf9f));
        graphics.setGradientFill(paper);
        graphics.fillRoundedRectangle(glass.toFloat(), 8.0f);

        graphics.setColour(juce::Colour(0xff2d241a));
        graphics.drawRoundedRectangle(glass.toFloat(), 8.0f, 2.0f);
        graphics.setColour(juce::Colour(0x45ffffff));
        graphics.drawRoundedRectangle(glass.toFloat().reduced(4.0f), 6.0f, 1.0f);

        const auto meterArea = glass.reduced(24, 20).toFloat();
        const auto centre = juce::Point<float>(meterArea.getCentreX(), meterArea.getBottom() + 12.0f);
        const auto radius = std::min(meterArea.getWidth() * 0.56f, meterArea.getHeight() * 0.88f);
        constexpr float leftDeg = 220.0f;
        constexpr float rightDeg = 320.0f;

        graphics.setColour(juce::Colour(palette::meterInk).withAlpha(0.78f));
        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, leftDeg * juce::MathConstants<float>::pi / 180.0f,
                          rightDeg * juce::MathConstants<float>::pi / 180.0f, true);
        graphics.strokePath(arc, juce::PathStrokeType(1.8f));

        constexpr std::array<int, 6> labels {24, 18, 12, 6, 3, 0};
        graphics.setFont(uiFont(15.0f));

        for (const auto value : labels) {
            const auto angle = juce::jmap(static_cast<float>(value), 24.0f, 0.0f, leftDeg, rightDeg);
            const auto outer = polarPoint(centre, radius, angle);
            const auto inner = polarPoint(centre, radius - 20.0f, angle);
            const auto textPoint = polarPoint(centre, radius - 44.0f, angle);
            graphics.setColour(value == 0 ? juce::Colour(0xff7f1d14) : juce::Colour(palette::meterInk));
            graphics.drawLine({inner, outer}, value == 0 ? 2.0f : 1.6f);
            graphics.drawText(juce::String(value),
                              juce::roundToInt(textPoint.x - 18.0f),
                              juce::roundToInt(textPoint.y - 9.0f),
                              36,
                              18,
                              juce::Justification::centred);
        }

        for (int minor = 1; minor < 24; ++minor) {
            if (minor == 3 || minor == 6 || minor == 12 || minor == 18) {
                continue;
            }

            const auto angle = juce::jmap(static_cast<float>(minor), 24.0f, 0.0f, leftDeg, rightDeg);
            graphics.setColour(juce::Colour(palette::meterInk).withAlpha(0.46f));
            graphics.drawLine({polarPoint(centre, radius - 10.0f, angle), polarPoint(centre, radius, angle)}, 0.8f);
        }

        const auto needleAngle = juce::jmap(juce::jlimit(0.0f, 24.0f, gainReductionDb_), 24.0f, 0.0f, leftDeg, rightDeg);
        const auto needleEnd = polarPoint(centre, radius - 24.0f, needleAngle);
        graphics.setColour(juce::Colour(0x66000000));
        graphics.drawLine({centre.translated(1.5f, 1.5f), needleEnd.translated(1.5f, 1.5f)}, 4.0f);
        graphics.setColour(juce::Colour(0xff111113));
        graphics.drawLine({centre, needleEnd}, 2.6f);
        graphics.setColour(juce::Colour(0xffece3d2));
        graphics.fillEllipse(juce::Rectangle<float>(18.0f, 18.0f).withCentre(centre));
        graphics.setColour(juce::Colour(0xff111113));
        graphics.fillEllipse(juce::Rectangle<float>(8.0f, 8.0f).withCentre(centre));

        graphics.setColour(juce::Colour(palette::meterInk));
        graphics.setFont(uiFont(24.0f));
        graphics.drawText("dB", glass.withY(glass.getY() + 34).withHeight(30), juce::Justification::centred);
        graphics.setFont(uiFont(12.0f));
        graphics.drawText("COMPRESSION", glass.withY(glass.getY() + 63).withHeight(18), juce::Justification::centred);
    }

private:
    float gainReductionDb_ {};
    std::uint64_t sequence_ {};
};

class SlimLevelMeter final : public juce::Component {
public:
    explicit SlimLevelMeter(juce::String label)
        : label_(std::move(label))
    {
    }

    void setLevel(float db, bool clip)
    {
        if (std::abs(db - db_) < 0.15f && clip == clip_) {
            return;
        }

        db_ = db;
        clip_ = clip;
        repaint();
    }

    void paint(juce::Graphics& graphics) override
    {
        auto bounds = getLocalBounds();
        graphics.setColour(juce::Colour(palette::silkMuted));
        graphics.setFont(uiFont(12.0f));
        graphics.drawText(label_, bounds.removeFromTop(18), juce::Justification::centred);

        auto meterFrame = bounds.withHeight(18).withCentre(bounds.getCentre()).reduced(4, 0);
        auto meter = meterFrame.withTrimmedRight(12);
        const auto clipLamp = meterFrame.removeFromRight(10).withSizeKeepingCentre(8, 8);

        graphics.setColour(juce::Colour(0xff050607));
        graphics.fillRoundedRectangle(meter.toFloat(), 4.0f);

        const auto limitedDb = juce::jlimit(-60.0f, 0.0f, db_);
        const auto proportion = juce::jmap(limitedDb, -60.0f, 0.0f, 0.0f, 1.0f);
        auto fill = meter.withTrimmedRight(juce::roundToInt(static_cast<float>(meter.getWidth()) * (1.0f - proportion)));

        const auto hot = limitedDb > -6.0f;
        juce::ColourGradient fillGradient(
            hot ? juce::Colour(0xffffbf6b) : juce::Colour(0xff4f8aa4),
            static_cast<float>(fill.getX()),
            static_cast<float>(fill.getY()),
            hot ? juce::Colour(0xffb7352e) : juce::Colour(0xff1e5167),
            static_cast<float>(fill.getRight()),
            static_cast<float>(fill.getY()),
            false);
        graphics.setGradientFill(fillGradient);
        if (fill.getWidth() > 4) {
            graphics.fillRoundedRectangle(fill.toFloat().reduced(2.0f, 2.0f), 3.0f);
        }

        graphics.setColour(juce::Colour(palette::silkMuted).withAlpha(0.16f));
        for (int tick = 1; tick < 3; ++tick) {
            const auto x = meter.getX() + (meter.getWidth() * tick) / 3;
            graphics.drawVerticalLine(x, static_cast<float>(meter.getY() + 5), static_cast<float>(meter.getBottom() - 5));
        }

        graphics.setColour(clip_ ? juce::Colour(palette::redLamp) : juce::Colour(0xff342f26));
        graphics.fillEllipse(clipLamp.toFloat());
        graphics.setColour(juce::Colour(palette::brassDark));
        graphics.drawRoundedRectangle(meter.toFloat(), 4.0f, 1.0f);
    }

private:
    juce::String label_;
    float db_ {u273::core::kMinMeterDb};
    bool clip_ {};
};

struct Layout {
    juce::Rectangle<int> panel;
    juce::Rectangle<int> header;
    juce::Rectangle<int> body;
    juce::Rectangle<int> controlsPanel;
    juce::Rectangle<int> meterBay;
    juce::Rectangle<int> bottomStrip;
};

[[nodiscard]] Layout makeLayout(juce::Rectangle<int> bounds)
{
    Layout layout {};
    layout.panel = bounds.reduced(18);

    auto content = layout.panel.reduced(22, 20);
    layout.header = content.removeFromTop(92);
    content.removeFromTop(10);
    layout.bottomStrip = content.removeFromBottom(116);
    content.removeFromBottom(12);
    layout.body = content;

    auto body = layout.body;
    layout.controlsPanel = body.removeFromLeft(524);
    body.removeFromLeft(26);
    layout.meterBay = body;
    return layout;
}

} // namespace

struct PluginEditor::Impl {
    Impl(PluginEditor& owner, U273AudioProcessor& processor)
        : owner_(owner)
        , processor_(processor)
        , inputKnob_(processor.parameters(), u273::core::param_id::inputGainDb, "INPUT", formatDb, parsePlain, juce::Colour(palette::silk))
        , driveKnob_(processor.parameters(), u273::core::param_id::drive, "DRIVE", formatPercent, [](const juce::String& text) {
            return parseNumber(text, 0.01);
        }, juce::Colour(0xffd9ad5d))
        , detectorKnob_(processor.parameters(), u273::core::param_id::detectorScale, "DETECTOR", formatScale, parsePlain, juce::Colour(0xffd56c5d))
        , calibrationKnob_(processor.parameters(), u273::core::param_id::calibrationLevelDb, "CAL LEVEL", formatDb, parsePlain, juce::Colour(palette::silkMuted))
        , attackKnob_(processor.parameters(), u273::core::param_id::attackMs, "ATTACK", formatMs, parsePlain, juce::Colour(palette::brass))
        , releaseKnob_(processor.parameters(), u273::core::param_id::releaseMs, "RELEASE", formatMs, parsePlain, juce::Colour(palette::brass))
        , mixKnob_(processor.parameters(), u273::core::param_id::mix, "MIX", formatPercent, [](const juce::String& text) {
            return parseNumber(text, 0.01);
        }, juce::Colour(palette::blueAccent))
        , outputKnob_(processor.parameters(), u273::core::param_id::outputGainDb, "OUTPUT", formatDb, parsePlain, juce::Colour(palette::silk))
        , inputMeter_("IN")
        , outputMeter_("OUT")
    {
        owner_.setLookAndFeel(&lookAndFeel_);

        add(inputKnob_);
        add(driveKnob_);
        add(detectorKnob_);
        add(calibrationKnob_);
        add(attackKnob_);
        add(releaseKnob_);
        add(mixKnob_);
        add(outputKnob_);
        add(gainReductionMeter_);
        add(inputMeter_);
        add(outputMeter_);

        bypassButton_.setButtonText("BYPASS");
        bypassButton_.setClickingTogglesState(true);
        bypassButton_.setConnectedEdges(0);
        bypassButton_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff6f241f));
        add(bypassButton_);
        bypassAttachment_ = std::make_unique<ButtonAttachment>(processor_.parameters(), juceId(u273::core::param_id::bypass), bypassButton_);
    }

    ~Impl()
    {
        owner_.setLookAndFeel(nullptr);
    }

    void paint(juce::Graphics& graphics)
    {
        graphics.fillAll(juce::Colour(palette::background));

        juce::Graphics::ScopedSaveState savedState(graphics);
        graphics.addTransform(juce::AffineTransform::scale(kInitialEditorScale));

        const auto layout = makeLayout(designBounds());

        drawInsetPanel(graphics, layout.panel, 14.0f);
        drawTexture(graphics, layout.panel.reduced(4));
        drawHeader(graphics, layout.header);
        drawMainSeparators(graphics, layout);
        drawControlsSilk(graphics, layout.controlsPanel);
        drawMeterBaySilk(graphics, layout.meterBay);
        drawBottomSilk(graphics, layout.bottomStrip, processor_.gainCellOversamplingFactor());
    }

    void resized()
    {
        const auto layout = makeLayout(designBounds());

        auto controls = layout.controlsPanel.reduced(4, 2);
        constexpr int rowGap = 24;
        const auto rowHeight = (controls.getHeight() - rowGap) / 2;
        auto firstRow = controls.removeFromTop(rowHeight);
        controls.removeFromTop(rowGap);
        auto secondRow = controls.removeFromTop(rowHeight);

        const auto takeCell = [](juce::Rectangle<int>& row, int remaining) {
            constexpr int gap = 14;
            const auto cellWidth = (row.getWidth() - gap * (remaining - 1)) / remaining;
            auto cell = row.removeFromLeft(cellWidth);
            if (remaining > 1) {
                row.removeFromLeft(gap);
            }
            return cell.reduced(2, 0);
        };

        inputKnob_.setBounds(takeCell(firstRow, 4));
        driveKnob_.setBounds(takeCell(firstRow, 3));
        detectorKnob_.setBounds(takeCell(firstRow, 2));
        calibrationKnob_.setBounds(takeCell(firstRow, 1));

        releaseKnob_.setBounds(takeCell(secondRow, 4));
        attackKnob_.setBounds(takeCell(secondRow, 3));
        outputKnob_.setBounds(takeCell(secondRow, 2));
        mixKnob_.setBounds(takeCell(secondRow, 1));

        auto meterBay = layout.meterBay.reduced(2, 0);
        gainReductionMeter_.setBounds(meterBay.removeFromTop(326).withSizeKeepingCentre(meterBay.getWidth(), 318));

        auto bottom = layout.bottomStrip.reduced(16, 8);
        bypassButton_.setBounds(bottom.removeFromLeft(122).withSizeKeepingCentre(100, 54));

        auto bottomMeters = bottom.removeFromRight(338).withSizeKeepingCentre(324, 74);
        inputMeter_.setBounds(bottomMeters.removeFromLeft(bottomMeters.getWidth() / 2).reduced(14, 0));
        outputMeter_.setBounds(bottomMeters.reduced(14, 0));

        applyInitialScale();
    }

    void updateMeters()
    {
        const auto frame = processor_.meterBridge().readLatest();
        gainReductionMeter_.setFrame(frame);
        inputMeter_.setLevel(frame.inputPeakDb, frame.clipFlag);
        outputMeter_.setLevel(frame.outputPeakDb, frame.clipFlag);
    }

private:
    void add(juce::Component& component)
    {
        owner_.addAndMakeVisible(component);
    }

    void applyInitialScale()
    {
        const auto transform = juce::AffineTransform::scale(kInitialEditorScale);

        inputKnob_.setTransform(transform);
        driveKnob_.setTransform(transform);
        detectorKnob_.setTransform(transform);
        calibrationKnob_.setTransform(transform);
        attackKnob_.setTransform(transform);
        releaseKnob_.setTransform(transform);
        mixKnob_.setTransform(transform);
        outputKnob_.setTransform(transform);
        gainReductionMeter_.setTransform(transform);
        inputMeter_.setTransform(transform);
        outputMeter_.setTransform(transform);
        bypassButton_.setTransform(transform);
    }

    static void drawStatusButton(juce::Graphics& graphics, juce::Rectangle<int> bounds, const juce::String& text, bool active)
    {
        drawFirstPassButtonFace(graphics, bounds, active, false, false);
        graphics.setColour(active ? juce::Colour(0xfff4efe3) : juce::Colour(palette::silkMuted));
        graphics.setFont(uiFont(13.0f, juce::Font::bold));
        graphics.drawText(text, bounds, juce::Justification::centred);
    }

    void drawHeader(juce::Graphics& graphics, juce::Rectangle<int> header) const
    {
        auto left = header;
        auto brand = left.removeFromLeft(172).reduced(6, 10);
        graphics.setColour(juce::Colour(palette::silk));
        graphics.setFont(uiFont(22.0f, juce::Font::bold));
        graphics.drawText("U73 Limiter", brand.removeFromTop(34), juce::Justification::centred);
        graphics.setFont(uiFont(13.0f));
        graphics.setColour(juce::Colour(palette::silkMuted));
        graphics.drawText("VINTAGE GAIN CELL", brand.removeFromTop(24), juce::Justification::centred);

        auto title = header.withTrimmedLeft(190).withTrimmedRight(190).reduced(0, 12);
        graphics.setColour(juce::Colour(palette::silk));
        graphics.setFont(uiFont(27.0f));
        graphics.drawText("VINTAGE LIMITER", title.removeFromTop(40), juce::Justification::centred);
        graphics.setFont(uiFont(18.0f));
        graphics.setColour(juce::Colour(palette::silkMuted));
        graphics.drawText("U73", title.removeFromTop(26), juce::Justification::centred);
    }

    static void drawMainSeparators(juce::Graphics& graphics, const Layout& layout)
    {
        graphics.setColour(juce::Colour(0x50000000));
        graphics.drawLine(static_cast<float>(layout.panel.getX() + 8),
                          static_cast<float>(layout.bottomStrip.getY() - 8),
                          static_cast<float>(layout.panel.getRight() - 8),
                          static_cast<float>(layout.bottomStrip.getY() - 8),
                          1.0f);
        graphics.setColour(juce::Colour(palette::panelEdge).withAlpha(0.10f));
        graphics.drawLine(static_cast<float>(layout.panel.getX() + 20),
                          static_cast<float>(layout.bottomStrip.getY() - 7),
                          static_cast<float>(layout.panel.getRight() - 20),
                          static_cast<float>(layout.bottomStrip.getY() - 7),
                          1.0f);

        graphics.setColour(juce::Colour(palette::panelEdge).withAlpha(0.14f));
        graphics.drawVerticalLine(layout.meterBay.getX() - 12,
                                  static_cast<float>(layout.body.getY() + 8),
                                  static_cast<float>(layout.body.getBottom() - 8));
    }

    static void drawControlsSilk(juce::Graphics& graphics, juce::Rectangle<int> controls)
    {
        graphics.setColour(juce::Colour(palette::panelEdge).withAlpha(0.10f));
        graphics.drawHorizontalLine(controls.getCentreY(),
                                    static_cast<float>(controls.getX() + 28),
                                    static_cast<float>(controls.getRight() - 28));
    }

    static void drawMeterBaySilk(juce::Graphics& graphics, juce::Rectangle<int> meterBay)
    {
        graphics.setColour(juce::Colour(palette::silkMuted).withAlpha(0.07f));
        graphics.setFont(uiFont(56.0f));
        graphics.drawText("U73", meterBay.withY(meterBay.getY() + 336).withHeight(70).reduced(14, 0), juce::Justification::centred);
    }

    static void drawBottomSilk(juce::Graphics& graphics, juce::Rectangle<int> bottom, int oversamplingFactor)
    {
        auto work = bottom.reduced(16, 8);
        work.removeFromLeft(122);
        work.removeFromRight(338);

        auto os = work.withSizeKeepingCentre(302, 84);
        graphics.setFont(uiFont(12.0f));
        graphics.setColour(juce::Colour(palette::silkMuted));
        graphics.drawText("OVERSAMPLING", os.removeFromTop(28), juce::Justification::centred);

        auto buttons = os.removeFromTop(44).reduced(6, 0);
        drawStatusButton(graphics, buttons.removeFromLeft(64), "1X", oversamplingFactor <= 1);
        buttons.removeFromLeft(12);
        drawStatusButton(graphics, buttons.removeFromLeft(64), "2X", oversamplingFactor == 2);
        buttons.removeFromLeft(12);
        drawStatusButton(graphics, buttons.removeFromLeft(72), "4X+", oversamplingFactor >= 4);
    }

    PluginEditor& owner_;
    U273AudioProcessor& processor_;
    U273LookAndFeel lookAndFeel_;
    ParameterKnob inputKnob_;
    ParameterKnob driveKnob_;
    ParameterKnob detectorKnob_;
    ParameterKnob calibrationKnob_;
    ParameterKnob attackKnob_;
    ParameterKnob releaseKnob_;
    ParameterKnob mixKnob_;
    ParameterKnob outputKnob_;
    GainReductionMeter gainReductionMeter_;
    SlimLevelMeter inputMeter_;
    SlimLevelMeter outputMeter_;
    juce::TextButton bypassButton_;
    std::unique_ptr<ButtonAttachment> bypassAttachment_;
};

PluginEditor::PluginEditor(U273AudioProcessor& processor)
    : AudioProcessorEditor(processor)
    , processor_(processor)
    , impl_(std::make_unique<Impl>(*this, processor))
{
    setSize(kInitialEditorWidth, kInitialEditorHeight);
    startTimerHz(30);
}

PluginEditor::~PluginEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void PluginEditor::paint(juce::Graphics& graphics)
{
    impl_->paint(graphics);
}

void PluginEditor::resized()
{
    impl_->resized();
}

void PluginEditor::timerCallback()
{
    impl_->updateMeters();
}

} // namespace u273::plugin
