#pragma once
#include <JuceHeader.h>
#include "Taal.h"

class TablaProcessor;

namespace Palette
{
    inline const juce::Colour background  { 0xfff6f4ef };
    inline const juce::Colour panel       { 0xffffffff };
    inline const juce::Colour panelLight  { 0xffe6e2d8 };
    inline const juce::Colour text        { 0xff23242a };
    inline const juce::Colour dim         { 0xff6f727c };
    inline const juce::Colour accent      { 0xffe08a14 };   // saffron
    inline const juce::Colour accentDeep  { 0xfff3b45c };
    inline const juce::Colour bolBox      { 0xffeceae3 };
    inline const juce::Colour error       { 0xffc23a2e };
}

// The beat ruler + bol boxes, laid out in rows of 8 beats .
class TaalView : public juce::Component
{
public:
    TaalView (TablaProcessor& p, std::function<const Taal&()> getTaal);

    std::function<void (int)> onSelect;
    std::function<void (int, int)> onMove;      // from, to
    std::function<void()> onDelete;
    std::function<void()> onTogglePlay;
    std::function<void (int)> onPlayBol;

    void setSelected (int index) { selected = index; repaint(); }
    int getSelected() const { return selected; }
    int getRequiredHeight() const;
    void setWidth (int w);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    static constexpr int beatsPerRow = 8;
    static constexpr int marginX = 40;
    static constexpr int headerH = 84;
    static constexpr int rowH = 120;

    float pxPerBeat() const { return (float) (getWidth() - 2 * marginX) / (float) beatsPerRow; }
    int numRows() const;
    int indexAt (juce::Point<int> p) const;
    juce::Rectangle<float> segment (int row, double from, double to) const;

    TablaProcessor& processor;
    std::function<const Taal&()> taalFn;
    int selected = -1;
    int dragFrom = -1, dragTarget = -1;
    bool dragging = false;
};
