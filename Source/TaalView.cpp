#include "TaalView.h"
#include "PluginProcessor.h"

TaalView::TaalView (TablaProcessor& p, std::function<const Taal&()> getTaal)
    : processor (p), taalFn (std::move (getTaal))
{
    setWantsKeyboardFocus (true);
}

int TaalView::numRows() const
{
    return std::max (1, (int) std::ceil (taalFn().length() / beatsPerRow - 1.0e-9));
}

int TaalView::getRequiredHeight() const { return headerH + numRows() * rowH + 24; }

void TaalView::setWidth (int w) { setSize (std::max (w, 360), getRequiredHeight()); }

juce::Rectangle<float> TaalView::segment (int row, double from, double to) const
{
    const float x0 = (float) marginX + (float) (from - row * beatsPerRow) * pxPerBeat();
    const float x1 = (float) marginX + (float) (to - row * beatsPerRow) * pxPerBeat();
    return { x0, (float) (headerH + row * rowH + 40), x1 - x0, 52.0f };
}

int TaalView::indexAt (juce::Point<int> p) const
{
    if (p.y < headerH) return -1;
    const int row = (p.y - headerH) / rowH;
    if (row >= numRows()) return -1;
    const double beat = row * beatsPerRow + (double) (p.x - marginX) / pxPerBeat();
    if (beat < 0.0) return -1;
    return taalFn().indexAtBeat (beat);
}

void TaalView::paint (juce::Graphics& g)
{
    const auto& taal = taalFn();
    const double length = taal.length();
    const double playhead = processor.getPlayheadBeat();
    const int playingIndex = playhead >= 0.0 ? taal.indexAtBeat (playhead) : -1;

    g.fillAll (Palette::background);

    // header
    g.setColour (Palette::text);
    g.setFont (juce::FontOptions ("Georgia", 32.0f, juce::Font::plain));
    g.drawText (taal.name, 0, 10, getWidth(), 40, juce::Justification::centred);
    g.setColour (Palette::dim);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText ("Taal length: " + juce::String (length, 2) + " beats, Default tempo = " + juce::String (taal.defaultTempo, 2),
                0, 52, getWidth(), 20, juce::Justification::centred);

    const int rows = numRows();
    for (int r = 0; r < rows; ++r)
    {
        const float yRule = (float) (headerH + r * rowH + 14);
        const int beatsHere = (int) std::min<double> (beatsPerRow, std::max (0.0, length - r * beatsPerRow));
        const float xEnd = (float) marginX + (float) std::min<double> (beatsPerRow, length - r * beatsPerRow) * pxPerBeat();

        g.setColour (Palette::dim);
        g.drawHorizontalLine ((int) yRule, (float) marginX, xEnd);
        g.setFont (juce::FontOptions (12.0f));
        for (int b = 0; b <= beatsHere; ++b)
        {
            const float x = (float) marginX + (float) b * pxPerBeat();
            g.drawVerticalLine ((int) x, yRule - 6.0f, yRule + 2.0f);
            if (b < beatsHere || b == beatsHere)
                g.drawText (juce::String (r * beatsPerRow + b + 1), (int) x - 14, (int) yRule - 22, 28, 14, juce::Justification::centred);
        }
    }

    // bol boxes
    for (int i = 0; i < (int) taal.events.size(); ++i)
    {
        const auto& e = taal.events[(size_t) i];
        const double s = taal.startOf (i), eEnd = s + e.duration;
        const bool isRest = e.bol.equalsIgnoreCase ("Rest");

        for (int r = (int) (s / beatsPerRow); r <= (int) ((eEnd - 1.0e-9) / beatsPerRow) && r < rows; ++r)
        {
            auto box = segment (r, std::max (s, (double) r * beatsPerRow), std::min (eEnd, (double) (r + 1) * beatsPerRow))
                           .reduced (1.0f, 0.0f);
            const bool isSel = i == selected;
            const bool isPlaying = i == playingIndex;

            juce::Colour fill = isRest ? Palette::panel : Palette::bolBox;
            if (isPlaying) fill = Palette::accent.withMultipliedBrightness (0.9f);
            else if (isSel) fill = Palette::accentDeep.withAlpha (0.55f);
            if (dragging && i == dragTarget) fill = fill.brighter (0.3f);

            g.setColour (fill);
            g.fillRoundedRectangle (box, 3.0f);
            g.setColour (isSel ? Palette::accent : Palette::dim.withAlpha (0.6f));
            g.drawRoundedRectangle (box, 3.0f, isSel ? 2.0f : 1.0f);

            g.setColour (isPlaying ? juce::Colours::black : (isRest ? Palette::dim : Palette::text));
            g.setFont (juce::FontOptions (17.0f));
            auto label = isRest ? juce::String ("-") : e.bol;
            if (e.emphasisDb > 0.1f) label << " " << juce::String::repeatedString ("+", std::max (1, (int) (e.emphasisDb / 3.0f)));
            g.drawText (label, box.toNearestInt(), juce::Justification::centred, true);
        }
    }

    // playhead
    if (playhead >= 0.0 && playhead < length)
    {
        const int row = (int) (playhead / beatsPerRow);
        const float x = (float) marginX + (float) (playhead - row * beatsPerRow) * pxPerBeat();
        const float y0 = (float) (headerH + row * rowH + 10);
        g.setColour (Palette::accent);
        g.drawLine (x, y0, x, y0 + 86.0f, 2.5f);
    }

    if (taal.events.empty())
    {
        g.setColour (Palette::dim);
        g.setFont (juce::FontOptions (16.0f));
        g.drawText ("Empty taal - click a bol below to add it, or type bols in the box at the bottom.",
                    0, headerH + 20, getWidth(), 30, juce::Justification::centred);
    }
}

void TaalView::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    dragFrom = indexAt (e.getPosition());
    dragTarget = -1;
    dragging = false;
    selected = dragFrom;
    if (onSelect) onSelect (dragFrom);
    repaint();
}

void TaalView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFrom < 0) return;
    if (! dragging && e.getDistanceFromDragStart() > 6) dragging = true;
    if (dragging)
    {
        dragTarget = indexAt (e.getPosition());
        repaint();
    }
}

void TaalView::mouseUp (const juce::MouseEvent&)
{
    if (dragging && dragFrom >= 0 && dragTarget >= 0 && dragTarget != dragFrom && onMove)
    {
        const int from = dragFrom, to = dragTarget;
        dragging = false; dragFrom = dragTarget = -1;
        onMove (from, to);
        return;
    }
    dragging = false; dragFrom = dragTarget = -1;
    repaint();
}

void TaalView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int i = indexAt (e.getPosition());
    if (i >= 0 && onPlayBol) onPlayBol (i);
}

bool TaalView::keyPressed (const juce::KeyPress& k)
{
    const int n = (int) taalFn().events.size();
    if (k == juce::KeyPress::spaceKey) { if (onTogglePlay) onTogglePlay(); return true; }
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { if (onDelete) onDelete(); return true; }
    if (k == juce::KeyPress::leftKey && n > 0)  { selected = std::max (0, selected < 0 ? n - 1 : selected - 1); if (onSelect) onSelect (selected); return true; }
    if (k == juce::KeyPress::rightKey && n > 0) { selected = std::min (n - 1, selected + 1); if (onSelect) onSelect (selected); return true; }
    return false;
}
