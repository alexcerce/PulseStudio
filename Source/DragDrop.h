#pragma once

#include "Common.h"
#include "PluginMenu.h"
#include "DrumRack.h"

/** Internal drag and drop: tracks, devices and session clips, with labelled drag images. */
namespace DragDrop
{
    inline const String trackPrefix { "PulseTrack:" }, devicePrefix { "PulseDevice:" }, slotPrefix { "PulseSlot:" };

    inline bool is (const var& desc, const String& prefix) { return desc.toString().startsWith (prefix); }
    inline te::EditItemID idOf (const var& desc)            { return te::EditItemID::fromString (desc.toString().fromFirstOccurrenceOf (":", false, false)); }

    /** A rounded label in the item's colour, used as the drag image. */
    inline ScaledImage makeImage (const String& text, Colour c)
    {
        const float scale = 2.0f;
        Font f (FontOptions (13.0f, Font::bold));
        const int w = jmin (260, (int) GlyphArrangement::getStringWidth (f, text) + 30), h = 26;
        Image img (Image::ARGB, (int) (w * scale), (int) (h * scale), true);
        Graphics g (img);
        g.addTransform (AffineTransform::scale (scale));
        auto r = Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (1.0f);
        g.setColour (Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (r.translated (0.0f, 1.5f), 6.0f);
        g.setColour (c);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (c.getPerceivedBrightness() > 0.62f ? Colour (0xff15171b) : Colours::white);
        g.setFont (f);
        g.drawText (text, r.reduced (10.0f, 0.0f), Justification::centredLeft, true);
        return ScaledImage (img, scale);
    }

    inline void start (Component& source, const String& desc, const String& label, Colour c)
    {
        if (auto* container = DragAndDropContainer::findParentDragContainerFor (&source))
            if (! container->isDragAndDropActive())
                container->startDragging (desc, &source, makeImage (label, c), true);
    }

    inline te::Track* trackFrom (te::Edit& edit, const var& desc)
    {
        const auto id = idOf (desc);
        for (auto t : te::getAllTracks (edit))
            if (t->itemID == id) return t;
        return nullptr;
    }

    inline te::Plugin* pluginFrom (te::Edit& edit, const var& desc) { return edit.getPluginCache().getPluginFor (idOf (desc)).get(); }

    inline te::Clip* clipFrom (te::Edit& edit, const var& desc) { return te::findClipForID (edit, idOf (desc)); }

    /** Moves a track before another (or to the end when `before` is null). */
    inline void moveTrack (te::Track& t, te::Track* before)
    {
        if (&t == before) return;
        auto& edit = t.edit;
        edit.getUndoManager().beginNewTransaction ("Move track");
        if (before != nullptr)
            edit.moveTrack (&t, te::TrackInsertPoint (*before, true));
        else if (auto last = te::getAllTracks (edit).getLast(); last != nullptr && last != &t)
            edit.moveTrack (&t, te::TrackInsertPoint (*last, false));
    }

    /** A copy of a plugin for another place (a Drum Rack gets its own rack, not a link to the same one). */
    inline te::Plugin::Ptr duplicate (te::Plugin& p)
    {
        auto& edit = p.edit;
        p.flushPluginStateToValueTree();
        if (auto ri = dynamic_cast<te::RackInstance*> (&p); ri != nullptr && ri->type != nullptr)
        {
            for (auto rp : ri->type->getPlugins()) rp->flushPluginStateToValueTree();
            auto rackState = ri->type->state.createCopy();
            te::EditItemID::remapIDs (rackState, nullptr, edit);
            if (auto newRack = edit.getRackList().addRackTypeFrom (rackState))
                return edit.getPluginCache().createNewPlugin (te::RackInstance::create (*newRack));
            return {};
        }
        auto v = p.state.createCopy();
        te::EditItemID::remapIDs (v, nullptr, edit);
        return edit.getPluginCache().createNewPlugin (v);
    }

    /** Moves (or copies) a device to a plugin list, before `before` (or before the track's fixed
        volume / meter at the end when `before` is null). */
    inline void placeDevice (te::Plugin& p, te::Track* destTrack, te::PluginList& dest, te::Plugin* before, bool copy)
    {
        auto& edit = p.edit;
        if (before == &p) return;
        edit.getUndoManager().beginNewTransaction (copy ? "Copy device" : "Move device");

        if (! copy && dest.indexOf (&p) >= 0)   // reorder within the same chain
        {
            auto parent = p.state.getParent();
            const int from = parent.indexOf (p.state);
            int to = -1;
            if (before != nullptr) to = parent.indexOf (before->state);
            else
            {
                auto* at = dynamic_cast<te::AudioTrack*> (destTrack);
                te::Plugin* fixedEnd = at != nullptr ? (te::Plugin*) at->getVolumePlugin() : nullptr;
                to = fixedEnd != nullptr ? parent.indexOf (fixedEnd->state) : parent.indexOf (dest[dest.size() - 1]->state) + 1;
            }
            if (to > from) --to;
            if (to >= 0 && to != from) parent.moveChild (from, to, &edit.getUndoManager());
            return;
        }

        te::Plugin::Ptr np;
        if (auto ri = dynamic_cast<te::RackInstance*> (&p); ri != nullptr && ! copy && ri->type != nullptr)
            np = edit.getPluginCache().createNewPlugin (te::RackInstance::create (*ri->type));   // same rack, new place
        else
            np = duplicate (p);
        if (np == nullptr) return;
        if (! copy) p.deleteFromParent();
        if (before != nullptr && dest.indexOf (before) >= 0)
            dest.insertPlugin (np, dest.indexOf (before), nullptr);
        else
            PluginMenu::insertPlugin (destTrack, dest, np, nullptr);
    }

    /** Moves (or copies) a session clip into a slot (replacing what was there). */
    inline void placeClipInSlot (te::Clip& c, te::ClipSlot& slot, bool copy)
    {
        if (slot.getClip() == &c) return;
        auto& edit = c.edit;
        edit.getUndoManager().beginNewTransaction (copy ? "Copy clip" : "Move clip");
        if (auto existing = slot.getClip()) existing->removeFromParent();
        if (copy) te::insertClipCopy (slot, te::ClipCopy::fromClip (c).withNewItemID (edit));
        else      c.moveTo (slot);
    }
}
