/*
 // Copyright (c) 2021-2022 Timothy Schoen and Pierre Guillot
 // For information on usage and redistribution, and for a DISCLAIMER OF ALL
 // WARRANTIES, see the file, "LICENSE.txt," in this distribution.
 */

#include "ObjectBase.h"

extern "C" {
#include <m_pd.h>
#include <g_canvas.h>
#include <m_imp.h>
#include <g_all_guis.h>
#include <g_undo.h>
}

#include "Object.h"
#include "Canvas.h"
#include "SuggestionComponent.h"
#include "PluginEditor.h"
#include "LookAndFeel.h"
#include "Pd/PdPatch.h"
#include "Utility/ObjectBoundsConstrainer.h"

#include "IEMHelper.h"
#include "AtomHelper.h"

#include "TextObject.h"
#include "ToggleObject.h"
#include "MessageObject.h"
#include "MouseObject.h"
#include "BangObject.h"
#include "ButtonObject.h"
#include "RadioObject.h"
#include "SliderObject.h"
#include "ArrayObject.h"
#include "GraphOnParent.h"
#include "KeyboardObject.h"
#include "KeyObject.h"
#include "MessboxObject.h"
#include "MousePadObject.h"
#include "NumberObject.h"
#include "NumboxTildeObject.h"
#include "CanvasObject.h"
#include "PictureObject.h"
#include "VUMeterObject.h"
#include "ListObject.h"
#include "SubpatchObject.h"
#include "CloneObject.h"
#include "CommentObject.h"
#include "CycloneCommentObject.h"
#include "FloatAtomObject.h"
#include "SymbolAtomObject.h"
#include "ScalarObject.h"
#include "TextDefineObject.h"
#include "CanvasListenerObjects.h"
#include "ScopeObject.h"
#include "FunctionObject.h"
#include "BicoeffObject.h"

// Class for non-patchable objects
class NonPatchable : public ObjectBase {

public:
    NonPatchable(void* obj, Object* parent)
        : ObjectBase(obj, parent)
    {
        parent->setVisible(false);
    }

    void updateBounds() override {};
    void applyBounds() override {};
};

void ObjectLabel::ObjectListener::componentMovedOrResized(Component& component, bool moved, bool resized)
{
    dynamic_cast<Object&>(component).gui->updateLabel();
}

ObjectBase::ObjectBase(void* obj, Object* parent)
    : ptr(obj)
    , object(parent)
    , cnv(parent->cnv)
    , pd(parent->cnv->pd)
{
    pd->registerMessageListener(ptr, this);

    updateLabel(); // TODO: fix virtual call from constructor

    setWantsKeyboardFocus(true);

    setLookAndFeel(new PlugDataLook());

    MessageManager::callAsync([_this = SafePointer<ObjectBase>(this)] {
        if (_this) {
            _this->initialiseParameters();
        }
    });
}

ObjectBase::~ObjectBase()
{
    pd->unregisterMessageListener(ptr, this);

    auto* lnf = &getLookAndFeel();
    setLookAndFeel(nullptr);
    delete lnf;
}

String ObjectBase::getText()
{
    if (!cnv->patch.checkObject(ptr))
        return "";

    cnv->pd->setThis();

    char* text = nullptr;
    int size = 0;
    libpd_get_object_text(ptr, &text, &size);

    if (text && size) {

        auto txt = String::fromUTF8(text, size);
        freebytes(static_cast<void*>(text), static_cast<size_t>(size) * sizeof(char));
        return txt;
    }

    return "";
}

String ObjectBase::getType() const
{
    ScopedLock lock(*pd->getCallbackLock());

    if (ptr) {
        // Check if it's an abstraction or subpatch
        if (pd_class(static_cast<t_pd*>(ptr)) == canvas_class && canvas_isabstraction((t_canvas*)ptr)) {
            char namebuf[MAXPDSTRING];
            t_object* ob = (t_object*)ptr;
            int ac = binbuf_getnatom(ob->te_binbuf);
            t_atom* av = binbuf_getvec(ob->te_binbuf);
            if (ac < 1)
                return String();
            atom_string(av, namebuf, MAXPDSTRING);

            return String::fromUTF8(namebuf).fromLastOccurrenceOf("/", false, false);
        }
        // Deal with different text objects
        if (String::fromUTF8(libpd_get_object_class_name(ptr)) == "text" && static_cast<t_text*>(ptr)->te_type == T_OBJECT) {
            return String("invalid");
        }
        if (String::fromUTF8(libpd_get_object_class_name(ptr)) == "text" && static_cast<t_text*>(ptr)->te_type == T_TEXT) {
            return String("comment");
        }
        if (String::fromUTF8(libpd_get_object_class_name(ptr)) == "text" && static_cast<t_text*>(ptr)->te_type == T_MESSAGE) {
            return String("message");
        }
        // Deal with atoms
        if (String::fromUTF8(libpd_get_object_class_name(ptr)) == "gatom") {
            if (static_cast<t_fake_gatom*>(ptr)->a_flavor == A_FLOAT)
                return "floatbox";
            else if (static_cast<t_fake_gatom*>(ptr)->a_flavor == A_SYMBOL)
                return "symbolbox";
            else if (static_cast<t_fake_gatom*>(ptr)->a_flavor == A_NULL)
                return "listbox";
        }
        // Get class name for all other objects
        if (auto* name = libpd_get_object_class_name(ptr)) {
            return String::fromUTF8(name);
        }
    }

    sys_unlock();

    return {};
}

// Called in destructor of subpatch and graph class
// Makes sure that any tabs refering to the now deleted patch will be closed
void ObjectBase::closeOpenedSubpatchers()
{
    auto* editor = object->cnv->editor;
    auto* tabbar = &editor->tabbar;

    if (!tabbar)
        return;

    for (int n = tabbar->getNumTabs() - 1; n >= 0; n--) {
        auto* cnv = editor->getCanvas(n);
        if (cnv && cnv->patch == *getPatch()) {
            auto* deletedPatch = &cnv->patch;

            editor->canvases.removeObject(cnv);
            tabbar->removeTab(n);

            editor->pd->patches.removeObject(deletedPatch, false);

            break;
        }
    }

    // Makes the tabbar check if it needs to hide
    if (tabbar->getNumTabs() == 0) {
        tabbar->currentTabChanged(-1, String());
    }

    MessageManager::callAsync([this, safeTabbar = SafePointer(tabbar)]() {
        if (!safeTabbar)
            return;

        safeTabbar->setCurrentTabIndex(safeTabbar->getNumTabs() - 1, true);
    });
}

void ObjectBase::openSubpatch()
{
    auto* subpatch = getPatch();

    if (!subpatch)
        return;

    auto* glist = subpatch->getPointer();

    if (!glist)
        return;

    auto abstraction = canvas_isabstraction(glist);
    File path;

    if (abstraction) {
        path = File(String::fromUTF8(canvas_getdir(subpatch->getPointer())->s_name)).getChildFile(String::fromUTF8(glist->gl_name->s_name)).withFileExtension("pd");
    }

    for (int n = 0; n < cnv->editor->tabbar.getNumTabs(); n++) {
        auto* tabCanvas = cnv->editor->getCanvas(n);
        if (tabCanvas->patch == *subpatch) {
            cnv->editor->tabbar.setCurrentTabIndex(n);
            return;
        }
    }

    auto* newPatch = cnv->editor->pd->patches.add(new pd::Patch(*subpatch));
    auto* newCanvas = cnv->editor->canvases.add(new Canvas(cnv->editor, *newPatch, nullptr));

    newPatch->setCurrentFile(path);

    cnv->editor->addTab(newCanvas);
    newCanvas->checkBounds();
}

void ObjectBase::moveToFront()
{
    pd->setThis();
    libpd_tofront(cnv->patch.getPointer(), static_cast<t_gobj*>(ptr));
}

void ObjectBase::moveToBack()
{
    pd->setThis();
    libpd_toback(cnv->patch.getPointer(), static_cast<t_gobj*>(ptr));
}

void ObjectBase::paint(Graphics& g)
{
    // make sure text is readable
    // TODO: move this to places where it's relevant
    getLookAndFeel().setColour(Label::textColourId, object->findColour(PlugDataColour::canvasTextColourId));
    getLookAndFeel().setColour(Label::textWhenEditingColourId, object->findColour(PlugDataColour::canvasTextColourId));
    getLookAndFeel().setColour(TextEditor::textColourId, object->findColour(PlugDataColour::canvasTextColourId));

    g.setColour(object->findColour(PlugDataColour::guiObjectBackgroundColourId));
    g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), PlugDataLook::objectCornerRadius);

    bool selected = cnv->isSelected(object) && !cnv->isGraph;
    auto outlineColour = object->findColour(selected ? PlugDataColour::objectSelectedOutlineColourId : objectOutlineColourId);

    g.setColour(outlineColour);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), PlugDataLook::objectCornerRadius, 1.0f);
}

void ObjectBase::initialiseParameters()
{
    getLookAndFeel().setColour(Label::textWhenEditingColourId, object->findColour(Label::textWhenEditingColourId));
    getLookAndFeel().setColour(Label::textColourId, object->findColour(Label::textColourId));

    auto params = getParameters();
    for (auto& [name, type, cat, value, list] : params) {
        value->addListener(this);

        // Push current parameters to pd
        valueChanged(*value);
    }

    repaint();
}

ObjectParameters ObjectBase::getParameters()
{
    return {};
}

void ObjectBase::startEdition()
{
    edited = true;
    pd->enqueueMessages("gui", "mouse", { 1.f });
}

void ObjectBase::stopEdition()
{
    edited = false;
    pd->enqueueMessages("gui", "mouse", { 0.f });
}

void ObjectBase::sendFloatValue(float newValue)
{
    cnv->pd->enqueueDirectMessages(ptr, newValue);
}

ObjectBase* ObjectBase::createGui(void* ptr, Object* parent)
{
    const String name = libpd_get_object_class_name(ptr);
    if (name == "bng") {
        return new BangObject(ptr, parent);
    }
    if (name == "button") {
        return new ButtonObject(ptr, parent);
    }
    if (name == "hsl" || name == "vsl" || name == "slider") {
        return new SliderObject(ptr, parent);
    }
    if (name == "tgl") {
        return new ToggleObject(ptr, parent);
    }
    if (name == "nbx") {
        return new NumberObject(ptr, parent);
    }
    if (name == "numbox~") {
        return new NumboxTildeObject(ptr, parent);
    }
    if (name == "vradio" || name == "hradio") {
        return new RadioObject(ptr, parent);
    }
    if (name == "cnv") {
        return new CanvasObject(ptr, parent);
    }
    if (name == "vu") {
        return new VUMeterObject(ptr, parent);
    }
    if (name == "text") {
        auto* textObj = static_cast<t_text*>(ptr);
        if (textObj->te_type == T_OBJECT) {
            return new TextObject(ptr, parent, false);
        } else {
            return new CommentObject(ptr, parent);
        }
    }
    if (name == "comment") {
        return new CycloneCommentObject(ptr, parent);
    }
    // Check if message type text object to prevent confusing it with else/message
    if (name == "message" && libpd_is_text_object(ptr) && static_cast<t_text*>(ptr)->te_type == T_MESSAGE) {
        return new MessageObject(ptr, parent);
    } else if (name == "pad") {
        return new MousePadObject(ptr, parent);
    } else if (name == "mouse") {
        return new MouseObject(ptr, parent);
    } else if (name == "keyboard") {
        return new KeyboardObject(ptr, parent);
    } else if (name == "pic") {
        return new PictureObject(ptr, parent);
    } else if (name == "text define") {
        return new TextDefineObject(ptr, parent);
    } else if (name == "gatom") {
        if (static_cast<t_fake_gatom*>(ptr)->a_flavor == A_FLOAT)
            return new FloatAtomObject(ptr, parent);
        else if (static_cast<t_fake_gatom*>(ptr)->a_flavor == A_SYMBOL)
            return new SymbolAtomObject(ptr, parent);
        else if (static_cast<t_fake_gatom*>(ptr)->a_flavor == A_NULL)
            return new ListObject(ptr, parent);
    } else if (name == "canvas" || name == "graph") {
        if (static_cast<t_canvas*>(ptr)->gl_list) {
            t_class* c = static_cast<t_canvas*>(ptr)->gl_list->g_pd;
            if (c && c->c_name && (String::fromUTF8(c->c_name->s_name) == "array")) {
                return new ArrayObject(ptr, parent);
            } else if (static_cast<t_canvas*>(ptr)->gl_isgraph) {
                return new GraphOnParent(ptr, parent);
            } else { // abstraction or subpatch
                return new SubpatchObject(ptr, parent);
            }
        } else if (static_cast<t_canvas*>(ptr)->gl_isgraph) {
            return new GraphOnParent(ptr, parent);
        } else {
            return new SubpatchObject(ptr, parent);
        }
    } else if (name == "array define") {
        return new ArrayDefineObject(ptr, parent);
    } else if (name == "clone") {
        return new CloneObject(ptr, parent);
    } else if (name == "pd") {
        return new SubpatchObject(ptr, parent);
    } else if (name == "scalar") {
        auto* gobj = static_cast<t_gobj*>(ptr);
        if (gobj->g_pd == scalar_class) {
            return new ScalarObject(ptr, parent);
        }
    } else if (name == "key") {
        return new KeyObject(ptr, parent, KeyObject::Key);
    } else if (name == "keyname") {
        return new KeyObject(ptr, parent, KeyObject::KeyName);
    } else if (name == "keyup") {
        return new KeyObject(ptr, parent, KeyObject::KeyUp);
    }
    // ELSE's [oscope~] and cyclone [scope~] are basically the same object
    else if (name == "oscope~") {
        return new OscopeObject(ptr, parent);
    } else if (name == "scope~") {
        return new ScopeObject(ptr, parent);
    } else if (name == "function") {
        return new FunctionObject(ptr, parent);
    } else if (name == "bicoeff") {
        return new BicoeffObject(ptr, parent);
    } else if (name == "messbox") {
        return new MessboxObject(ptr, parent);
    }else if (name == "canvas.active") {
        return new CanvasActiveObject(ptr, parent);
    } else if (name == "canvas.mouse") {
        return new CanvasMouseObject(ptr, parent);
    } else if (name == "canvas.vis") {
        return new CanvasVisibleObject(ptr, parent);
    } else if (name == "canvas.zoom") {
        return new CanvasZoomObject(ptr, parent);
    } else if (name == "canvas.edit") {
        return new CanvasEditObject(ptr, parent);
    } else if (!pd_checkobject(static_cast<t_pd*>(ptr))) {
        // Object is not a patcher object but something else
        return new NonPatchable(ptr, parent);
    }

    return new TextObject(ptr, parent);
}

bool ObjectBase::canOpenFromMenu()
{
    return zgetfn(static_cast<t_pd*>(ptr), pd->generateSymbol("menu-open")) != nullptr;
}

void ObjectBase::openFromMenu()
{
    pd_typedmess(static_cast<t_pd*>(ptr), pd->generateSymbol("menu-open"), 0, nullptr);
};

bool ObjectBase::hideInGraph()
{
    return false;
}

void ObjectBase::lock(bool isLocked)
{
    setInterceptsMouseClicks(isLocked, isLocked);
}

Canvas* ObjectBase::getCanvas()
{
    return nullptr;
};

pd::Patch* ObjectBase::getPatch()
{
    return nullptr;
};

bool ObjectBase::canReceiveMouseEvent(int x, int y)
{
    return true;
}

void ObjectBase::receiveMessage(String const& symbol, int argc, t_atom* argv)
{
    auto atoms = pd::Atom::fromAtoms(argc, argv);

    MessageManager::callAsync([_this = SafePointer(this), symbol, atoms]() mutable {
        if (!_this || _this->cnv->patch.objectWasDeleted(_this->ptr))
            return;

        if (symbol == "size" || symbol == "delta" || symbol == "pos" || symbol == "dim" || symbol == "width" || symbol == "height") {
            _this->updateBounds();
        } else {
            _this->receiveObjectMessage(symbol, atoms);
        }
    });
}

void ObjectBase::setParameterExcludingListener(Value& parameter, var value)
{
    parameter.removeListener(this);
    parameter.setValue(value);
    parameter.addListener(this);
}

ObjectLabel* ObjectBase::getLabel()
{
    return label.get();
}
bool ObjectBase::isBeingEdited()
{
    return edited;
}
