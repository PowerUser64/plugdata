/*
 // Copyright (c) 2021-2022 Timothy Schoen
 // For information on usage and redistribution, and for a DISCLAIMER OF ALL
 // WARRANTIES, see the file, "LICENSE.txt," in this distribution.
*/

#pragma once

#include <JuceHeader.h>

#include "Object.h"
#include "Pd/PdPatch.h"
#include "PluginProcessor.h"
#include "ObjectGrid.h"
#include "Utility/RateReducer.h"

class SuggestionComponent;
struct GraphArea;
class Iolet;
class PluginEditor;
class ConnectionPathUpdater;
class ConnectionBeingCreated;

class Canvas : public Component
    , public Value::Listener
    , public Timer
    , public LassoSource<WeakReference<Component>> {
public:
    Canvas(PluginEditor* parent, pd::Patch& patch, Component* parentGraph = nullptr);

    ~Canvas() override;

    PluginEditor* editor;
    PluginProcessor* pd;

    void paint(Graphics& g) override;

    void resized() override
    {
        repaint();
    }

    void timerCallback() override;
    bool rateLimit = true;

    void mouseDown(MouseEvent const& e) override;
    void mouseDrag(MouseEvent const& e) override;
    void mouseUp(MouseEvent const& e) override;
    void mouseMove(MouseEvent const& e) override;

    void synchronise(bool updatePosition = true);

    void updateDrawables();

    bool keyPressed(KeyPress const& key) override;
    void valueChanged(Value& v) override;

    void hideAllActiveEditors();

    void copySelection();
    void removeSelection();
    void pasteSelection();
    void duplicateSelection();

    void encapsulateSelection();

    bool canConnectSelectedObjects();
    bool connectSelectedObjects();

    void cancelConnectionCreation();

    void undo();
    void redo();

    void checkBounds();

    // Multi-dragger functions
    void deselectAll();
    void setSelected(Component* component, bool shouldNowBeSelected, bool updateCommandStatus = true);
    bool isSelected(Component* component) const;

    void objectMouseDown(Object* component, MouseEvent const& e);
    void objectMouseUp(Object* component, MouseEvent const& e);
    void objectMouseDrag(MouseEvent const& e);
    void checkMouseDragPositions();
        
    SelectedItemSet<WeakReference<Component>>& getLassoSelection() override;

    void removeSelectedComponent(Component* component);
    void findLassoItemsInArea(Array<WeakReference<Component>>& itemsFound, Rectangle<int> const& area) override;

    void updateSidebarSelection();

    void showSuggestions(Object* object, TextEditor* textEditor);
    void hideSuggestions();

    ObjectParameters& getInspectorParameters();

    template<typename T>
    Array<T*> getSelectionOfType()
    {
        Array<T*> result;

        for (auto obj : selectedComponents) {
            if (auto* objOfType = dynamic_cast<T*>(obj.get())) {
                result.add(objOfType);
            }
        }

        return result;
    }

    Viewport* viewport = nullptr;

    bool connectingWithDrag = false;
    SafePointer<Iolet> nearestIolet;

    pd::Patch& patch;

    // Needs to be allocated before object and connection so they can deselect themselves in the destructor
    SelectedItemSet<WeakReference<Component>> selectedComponents;

    OwnedArray<Object> objects;
    OwnedArray<Connection> connections;
    OwnedArray<ConnectionBeingCreated> connectionsBeingCreated;

    Value locked;
    Value commandLocked;
    Value presentationMode;
    Value gridEnabled = Value(var(true));

    bool isGraph = false;
    bool hasParentCanvas = false;
    bool updatingBounds = false; // used by connection
    bool isDraggingLasso = false;

    Value isGraphChild = Value(var(false));
    Value hideNameAndArgs = Value(var(false));
    Value xRange, yRange;

    ObjectGrid grid = ObjectGrid(this);

    Point<int> canvasOrigin = { 0, 0 };
    Point<int> canvasDragStartPosition = { 0, 0 };
    Point<int> viewportPositionBeforeMiddleDrag = { 0, 0 };

    GraphArea* graphArea = nullptr;
    SuggestionComponent* suggestor = nullptr;

    bool attachNextObjectToMouse = false;
    bool wasDragDuplicated = false;
    bool wasSelectedOnMouseDown = false;
    SafePointer<Object> lastSelectedObject = nullptr; // For auto patching
    SafePointer<Connection> lastSelectedConnection;   // For auto patching

    // Multi-dragger variables
    bool didStartDragging = false;
    int const minimumMovementToStartDrag = 5;
    SafePointer<Object> componentBeingDragged = nullptr;

    Point<int> lastMousePosition;
    Point<int> pastedPosition;
    Point<int> pastedPadding;
    std::map<Object*, Point<int>> mouseDownObjectPositions; // Stores object positions for alt + drag

    std::unique_ptr<ConnectionPathUpdater> pathUpdater;

private:
    SafePointer<Object> objectSnappingInbetween;
    SafePointer<Connection> connectionToSnapInbetween;
    SafePointer<TabbedComponent> tabbar;

    LassoComponent<WeakReference<Component>> lasso;

    RateReducer canvasRateReducer;
    RateReducer objectRateReducer;

    // Properties that can be shown in the inspector by right-clicking on canvas
    ObjectParameters parameters = { { "Is graph", tBool, cGeneral, &isGraphChild, { "No", "Yes" } },
        { "Hide name and arguments", tBool, cGeneral, &hideNameAndArgs, { "No", "Yes" } },
        { "X range", tRange, cGeneral, &xRange, {} },
        { "Y range", tRange, cGeneral, &yRange, {} } };
        
        
    struct DragContainer : public Component
    {
        DragContainer(Component* p) : parent(p) {
            setBufferedToImage(true);
        }
        
        void drag(Point<int> dragDistance)
        {
            if(!isDragging && !dragDistance.isOrigin()) {
                for(auto* component : draggedComponents)
                {
                    component->setTopLeftPosition(component->getPosition() - mouseDownPosition);
                    parent->removeChildComponent(component);
                    addAndMakeVisible(component);
                }
                toFront(false);
                parent->addAndMakeVisible(this);
                isDragging = true;
            }
            
            
            setTopLeftPosition(mouseDownPosition + dragDistance);
        }
        
        void beginDrag(Array<Component*> componentsToDrag, Array<Component*> all)
        {
            // Just to be sure
            allComponents = all;
            draggedComponents = componentsToDrag;
            
            Rectangle<int> totalBounds;
            for(auto* component : draggedComponents)
            {
                totalBounds = totalBounds.getUnion(component->getBounds());
            }
            
            setBounds(totalBounds);
            
            mouseDownPosition = totalBounds.getPosition();
        }
        

        void endDrag()
        {
            if(!isDragging) return;
            
            for(auto* component : draggedComponents)
            {                
                component->setTopLeftPosition(parent->getLocalPoint(this, component->getPosition()));
                removeChildComponent(component);
                parent->addAndMakeVisible(component);
            }
            
            // Restore original component order
            Component* last = nullptr;
            for(int i = allComponents.size() - 1; i >= 0; i--) {
                if(!last) {
                    allComponents[i]->toFront(false);
                }
                else {
                    allComponents[i]->toBehind(last);
                }
                last = allComponents[i];
            }
            
            parent->removeChildComponent(this);
            isDragging = false;
        }
        
        // This should never happen, but just to be sure
        // Better to call endDrag twice than not calling it at all, since it checks if its dragging anyway
        void mouseUp(const MouseEvent& e)
        {
            endDrag();
        }
        
        Array<Component*> getDraggedComponents() {
            return draggedComponents;
        }
        
        bool onlyOneSelected(Component* first, Component* second)
        {
            return draggedComponents.contains(first) ^ draggedComponents.contains(second);
        }
        
        bool eitherOneSelected(Component* first, Component* second)
        {
            return draggedComponents.contains(first) || draggedComponents.contains(second);
        }
           
        private:
            bool isDragging = false;
            Component* parent;
            Array<Component*> draggedComponents;
            Array<Component*> allComponents;
            Point<int> mouseDownPosition;
        };
        
    DragContainer dragContainer;
        

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Canvas)
};
