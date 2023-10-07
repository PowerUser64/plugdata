/*
 // Copyright (c) 2021-2022 Timothy Schoen.
 // For information on usage and redistribution, and for a DISCLAIMER OF ALL
 // WARRANTIES, see the file, "LICENSE.txt," in this distribution.
 */

#include "../Utility/PropertiesPanel.h"

class InspectorPanel : public PropertiesPanel {
};

class PropertyRedirector : public Value::Listener
{
public:
    
    PropertyRedirector(Value* controllerValue, Array<Value*> attachedValues) : values(attachedValues)
    {
        baseValue.referTo(*controllerValue);
        baseValue.addListener(this);
    }
    
    ~PropertyRedirector()
    {
        baseValue.removeListener(this);
    }
    
    void valueChanged(Value& v) override
    {
        for(auto* value : values)
        {
            value->setValue(baseValue.getValue());
        }
    }
    
    Value baseValue;
    Array<Value*> values;
};

class Inspector : public Component {

    InspectorPanel panel;
    String title;
    TextButton resetButton;
    Array<ObjectParameters> properties;
    OwnedArray<PropertyRedirector> redirectors;

public:
    Inspector()
    {
        panel.setTitleHeight(20);
        panel.setTitleAlignment(PropertiesPanel::AlignWithPropertyName);
        panel.setDrawShadowAndOutline(false);
        addAndMakeVisible(panel);
        lookAndFeelChanged();
    }

    void lookAndFeelChanged() override
    {
        panel.setSeparatorColour(findColour(PlugDataColour::sidebarBackgroundColourId));
        panel.setPanelColour(findColour(PlugDataColour::sidebarActiveBackgroundColourId));
    }

    void paint(Graphics& g) override
    {
        g.fillAll(findColour(PlugDataColour::sidebarBackgroundColourId));
    }

    void resized() override
    {
        panel.setBounds(getLocalBounds());
        resetButton.setTopLeftPosition(getLocalBounds().withTrimmedRight(23).getRight(), 0);

        panel.setContentWidth(getWidth() - 16);
    }

    void setTitle(String const& name)
    {
        title = name;
    }

    String getTitle()
    {
        return title;
    }

    static PropertiesPanel::Property* createPanel(int type, String const& name, Value* value, StringArray& options)
    {
        switch (type) {
        case tString:
            return new PropertiesPanel::EditableComponent<String>(name, *value);
        case tFloat:
            return new PropertiesPanel::EditableComponent<float>(name, *value);
        case tInt:
            return new PropertiesPanel::EditableComponent<int>(name, *value);
        case tColour:
            return new PropertiesPanel::ColourComponent(name, *value);
        case tBool:
            return new PropertiesPanel::BoolComponent(name, *value, options);
        case tCombo:
            return new PropertiesPanel::ComboComponent(name, *value, options);
        case tRangeFloat:
            return new PropertiesPanel::RangeComponent(name, *value, false);
        case tRangeInt:
            return new PropertiesPanel::RangeComponent(name, *value, true);
        case tFont:
            return new PropertiesPanel::FontComponent(name, *value);
        default:
            return new PropertiesPanel::EditableComponent<String>(name, *value);
        }
    }

    void showParameters()
    {
        loadParameters(properties);
    }

    void loadParameters(Array<ObjectParameters>& objectParameters)
    {
        properties = objectParameters;

        StringArray names = { "Dimensions", "General", "Appearance", "Label", "Extra" };

        panel.clear();

        auto parameterIsInAllObjects = [&objectParameters](ObjectParameter& param, Array<Value*>& values){
            
            auto& [name1, type1, category1, value1, options1, defaultVal1] = param;
            
            bool isInAllObjects = true;
            for(auto& parameters : objectParameters)
            {
                bool hasParameter = false;
                for(auto& [name2, type2, category2, value2, options2, defaultVal2] : parameters.getParameters())
                {
                    if(name1 == name2 && type1 == type2 && category1 == category2)
                    {
                        values.add(value2);
                        hasParameter = true;
                        break;
                    }
                }
                
                isInAllObjects = isInAllObjects && hasParameter;
            }
            
            return isInAllObjects;
        };
        
        redirectors.clear();
        
        for (int i = 0; i < 4; i++) {
            Array<PropertiesPanel::Property*> panels;
            int idx = 0;
            for (auto& parameter : objectParameters[0].getParameters()) {
                auto& [name, type, category, value, options, defaultVal] = parameter;
                if (static_cast<int>(category) == i) {
                    
                    Array<Value*> otherValues;
                    if(!parameterIsInAllObjects(parameter, otherValues)) continue;
                    
                    redirectors.add(new PropertyRedirector(value, otherValues));
                    
                    auto newPanel = createPanel(type, name, value, options);
                    newPanel->setPreferredHeight(26);
                    panels.add(newPanel);
                    idx++;
                }
            }
            if (!panels.isEmpty()) {
                panel.addSection(names[i], panels);
            }
        }
    }

    std::unique_ptr<Component> getExtraSettingsComponent()
    {
        auto* resetButton = new TextButton(Icons::Reset);
        resetButton->getProperties().set("Style", "SmallIcon");
        resetButton->setTooltip("Reset to default");
        resetButton->setSize(23, 23);
        resetButton->onClick = [this]() {
            //properties.resetAll();
        };

        return std::unique_ptr<TextButton>(resetButton);
    }
};
