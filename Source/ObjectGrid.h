/*
 // Copyright (c) 2021-2022 Timothy Schoen
 // For information on usage and redistribution, and for a DISCLAIMER OF ALL
 // WARRANTIES, see the file, "LICENSE.txt," in this distribution.
 */

#pragma once
#include <JuceHeader.h>
#include "Utility/SettingsFile.h"

enum GridType {
    NotSnappedToGrid = 0,
    HorizontalSnap = 1,
    VerticalSnap = 2,
    ConnectionSnap = 4,
};

inline GridType operator|(GridType a, GridType b)
{
    return static_cast<GridType>(static_cast<int>(a) | static_cast<int>(b));
}

inline GridType operator&(GridType a, GridType b)
{
    return static_cast<GridType>(static_cast<int>(a) & static_cast<int>(b));
}

class Object;
class Canvas;
class ObjectGrid : public SettingsFileListener {

public:
    ObjectGrid(Canvas* parent);

    Point<int> handleMouseDrag(Object* toDrag, Point<int> dragOffset, Rectangle<int> viewBounds);
    Point<int> handleMouseUp(Point<int> dragOffset);

    static constexpr int range = 5;
    static constexpr int tolerance = 3;

private:
    enum SnapOrientation {
        SnappedLeft,
        SnappedCentre,
        SnappedRight,
        SnappedConnection
    };

    bool snapped[2] = { false, false };

    SnapOrientation orientation[2];
    int idx[2];
    Point<int> position[2];
    Component::SafePointer<Component> start[2];
    Component::SafePointer<Component> end[2];
    DrawablePath gridLines[2];
    
    Canvas* cnv;

    int totalSnaps = 0;

    int gridEnabled = 1;

    Point<int> setState(bool isSnapped, int idx, Point<int> position, Component* start, Component* end, bool horizontal);
    void updateMarker();
    void clear(bool horizontal);

    Point<int> performVerticalSnap(Object* toDrag, Point<int> dragOffset, Rectangle<int> viewBounds);
    Point<int> performHorizontalSnap(Object* toDrag, Point<int> dragOffset, Rectangle<int> viewBounds);

    Point<int> performAbsoluteSnap(Object* toDrag, Point<int> dragOffset);

    bool trySnap(int distance);

    void propertyChanged(String name, var value) override;
};
