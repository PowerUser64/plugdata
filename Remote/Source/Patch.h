#pragma once

#include <JuceHeader.h>

#include <z_libpd.h>
#include <x_libpd_mod_utils.h>
#include <x_libpd_extra_utils.h>
#include <m_imp.h>
#include <g_canvas.h>
#include <g_undo.h>

extern "C" {

struct _instanceeditor {
    t_binbuf* copy_binbuf;
    char* canvas_textcopybuf;
    int canvas_textcopybufsize;
    t_undofn canvas_undo_fn;      /* current undo function if any */
    int canvas_undo_whatnext;     /* whether we can now UNDO or REDO */
    void* canvas_undo_buf;        /* data private to the undo function */
    t_canvas* canvas_undo_canvas; /* which canvas we can undo on */
    char const* canvas_undo_name;
    int canvas_undo_already_set_move;
    double canvas_upclicktime;
    int canvas_upx, canvas_upy;
    int canvas_find_index, canvas_find_wholeword;
    t_binbuf* canvas_findbuf;
    int paste_onset;
    t_canvas* paste_canvas;
    t_glist* canvas_last_glist;
    int canvas_last_glist_x, canvas_last_glist_y;
    t_canvas* canvas_cursorcanvaswas;
    unsigned int canvas_cursorwas;
};

static void canvas_bind(t_canvas* x)
{
    if (strcmp(x->gl_name->s_name, "Pd"))
        pd_bind(&x->gl_pd, canvas_makebindsym(x->gl_name));
}

static void canvas_unbind(t_canvas* x)
{
    if (strcmp(x->gl_name->s_name, "Pd"))
        pd_unbind(&x->gl_pd, canvas_makebindsym(x->gl_name));
}

void canvas_map(t_canvas* x, t_floatarg f);

void canvas_declare(t_canvas* x, t_symbol* s, int argc, t_atom* argv);
}

// False GATOM
typedef struct _fake_gatom {
    t_text a_text;
    int a_flavor;          /* A_FLOAT, A_SYMBOL, or A_LIST */
    t_glist* a_glist;      /* owning glist */
    t_float a_toggle;      /* value to toggle to */
    t_float a_draghi;      /* high end of drag range */
    t_float a_draglo;      /* low end of drag range */
    t_symbol* a_label;     /* symbol to show as label next to object */
    t_symbol* a_symfrom;   /* "receive" name -- bind ourselves to this */
    t_symbol* a_symto;     /* "send" name -- send to this on output */
    t_binbuf* a_revertbuf; /* binbuf to revert to if typing canceled */
    int a_dragindex;       /* index of atom being dragged */
    int a_fontsize;
    unsigned int a_shift : 1;         /* was shift key down when drag started? */
    unsigned int a_wherelabel : 2;    /* 0-3 for left, right, above, below */
    unsigned int a_grabbed : 1;       /* 1 if we've grabbed keyboard */
    unsigned int a_doubleclicked : 1; /* 1 if dragging from a double click */
    t_symbol* a_expanded_to;
} t_fake_gatom;

struct PointerWithId
{
    PointerWithId(void* destination) : ptr(destination)
    {
    }
    
    String getID() {
        return String(reinterpret_cast<intptr_t>(ptr));
    }
    
    template<typename T>
    T getPointer() {
        return static_cast<T>(ptr);
    }
    
    void* getPointer() {
        return ptr;
    }
    
private:
    
    void* ptr;
};

struct Object : public PointerWithId
{
    
    
    Object(MessageHandler& m, void* ptr, PointerWithId& patchPtr) : PointerWithId(ptr),
    messageHandler(m),
    patch(patchPtr)
    {

    }
    
    void synchronise() {
        MemoryOutputStream sync_message;
        
        sync_message.writeInt(MessageHandler::tObject);
        sync_message.writeString(patch.getID());
        sync_message.writeString(getID());
        sync_message.writeString("SetText");
        
        char* text;
        int length;
        libpd_get_object_text(getPointer(), &text, &length);
        
        sync_message.writeString(String(text, length));
        messageHandler.sendMessage(sync_message.getMemoryBlock());
        
        updateWidth();
    }
    
    void receiveMessage(MemoryBlock message)
    {
        MemoryInputStream istream(message, false);
        
        auto selector = istream.readString();
        if(selector == "SetWidth") {
            getPointer<t_text*>()->te_width = istream.readInt();
        }
        if(selector == "RequestSync") {
            synchronise();
        }
    }
    
    void updateWidth() {
        MemoryOutputStream message;
        message.writeInt(MessageHandler::tObject);
        message.writeString(patch.getID());
        message.writeString(getID());
        message.writeString("SetWidth");
        message.writeInt(getPointer<t_text*>()->te_width);
        
        messageHandler.sendMessage(message.getMemoryBlock());
    }
    
    Rectangle<int> getBounds() {
        int x, y, w, h;
        
        auto* obj = getPointer<t_object*>();
        libpd_get_object_bounds(patch.getPointer(), getPointer(), &x, &y, &w, &h);
        
        w *= 4.0f;
        h *= 4.0f;
        
        return {x, y, w + 15, h};
    }
    
    std::pair<std::vector<bool>, std::vector<bool>> getIolets() {

        auto inlets = std::vector<bool>(libpd_ninlets(getPointer<t_object*>()));
        auto outlets = std::vector<bool>(libpd_noutlets(getPointer<t_object*>()));
        
        for(int i = 0; i < inlets.size(); i++) {
            inlets[i] = libpd_issignalinlet(getPointer<t_object*>(), i);
        }
        for(int i = 0; i < outlets.size(); i++) {
            outlets[i] = libpd_issignaloutlet(getPointer<t_object*>(), i);
        }

        return {inlets, outlets};
    }
    
    String getName() {
        const String name = libpd_get_object_class_name(getPointer());
        
        if (name == "gatom") {
            auto* gatom = getPointer<t_fake_gatom*>();
            if (gatom->a_flavor == A_FLOAT)
                return "floatatom";
            else if (gatom->a_flavor == A_SYMBOL)
                return "symbolatom";
            else if (gatom->a_flavor == A_NULL)
                return "listatom";
        }
        
        else if (name == "canvas" || name == "graph") {
            auto* cnv = getPointer<t_canvas*>();
            if (cnv->gl_list) {
                t_class* c = cnv->gl_list->g_pd;
                if (c && c->c_name && (String::fromUTF8(c->c_name->s_name) == "array")) {
                    return "array";
                } else if (cnv->gl_isgraph) {
                    return "graph";
                } else { // abstraction or subpatch
                    return "subpatch";
                }
            } else if (cnv->gl_isgraph) {
                return "graph";
            } else {
                return "subpatch";
            }
        }
        
        return name;
    }
private:
    PointerWithId& patch;
    MessageHandler& messageHandler;
};


struct Connection
{
    int inlet;
    PointerWithId inbox;
    
    int outlet;
    PointerWithId outbox;
    
    Connection(int inlet_idx, PointerWithId start, int outlet_idx, PointerWithId end) :
    outlet(outlet_idx),
    outbox(start),
    inlet(inlet_idx),
    inbox(end)
    {
        
    }
};

struct Patch : public PointerWithId
{
    MessageHandler& messageHandler;
    
    Patch(MessageHandler& m, void* ptr) : PointerWithId(ptr), messageHandler(m)
    {
    }
    
    void* getObjectByID(String ID)
    {
        for(auto& object : objects)
        {
            if(object.getID() == ID)
            {
                return object.getPointer();
            }
        }
        
        return nullptr;
    }
    
    void synchronise() {
        
        update();
        
        MemoryOutputStream sync_message;
        
        sync_message.writeInt(MessageHandler::tPatch);
        sync_message.writeString(getID());
        
        sync_message.writeString("Sync");
        
        
        for(auto& object : objects) {
            
            sync_message.writeBool(false);
            sync_message.writeString(object.getID());
            sync_message.writeString(object.getName());
            
            auto bounds = object.getBounds();
            sync_message.writeInt(bounds.getX());
            sync_message.writeInt(bounds.getY());
            sync_message.writeInt(bounds.getWidth());
            sync_message.writeInt(bounds.getHeight());
            
            auto [inlets, outlets] = object.getIolets();
            
            sync_message.writeInt(inlets.size());
            for(auto inlet : inlets) {
                sync_message.writeBool(inlet);
            }
            
            sync_message.writeInt(outlets.size());
            for(auto outlet : outlets) {
                sync_message.writeBool(outlet);
            }
        }
        
        for(auto& connection : connections)
        {
            sync_message.writeBool(true);
            sync_message.writeInt(connection.outlet);
            sync_message.writeString(connection.outbox.getID());
            sync_message.writeInt(connection.inlet);
            sync_message.writeString(connection.inbox.getID());
        }
        
        messageHandler.sendMessage(sync_message.getMemoryBlock());
        
        
        for(auto& object : objects) {
            object.synchronise();
        }
    }
    
    void update() {
        
        objects.clear();
        auto* cnv = getPointer<t_canvas*>();
        
        for (t_gobj* y = cnv->gl_list; y; y = y->g_next) {
            
            objects.push_back(Object(messageHandler, static_cast<void*>(y), *this));
        }
        
        
        connections.clear();

        t_linetraverser t;
        // Get connections from pd
        linetraverser_start(&t, cnv);

        while (linetraverser_next(&t)) {
            
            connections.push_back(Connection(t.tr_inno, PointerWithId(t.tr_ob), t.tr_outno, PointerWithId(t.tr_ob2)));
        }
    }
    
    Rectangle<int> getBounds() const
    {
        /*
        if (ptr) {
            t_canvas* cnv = getPointer<t_canvas*>();

            if (cnv->gl_isgraph) {
                cnv->gl_pixwidth = std::max(15, cnv->gl_pixwidth);
                cnv->gl_pixheight = std::max(15, cnv->gl_pixheight);

                return { cnv->gl_xmargin, cnv->gl_ymargin, cnv->gl_pixwidth, cnv->gl_pixheight };
            }
        }
        return { 0, 0, 0, 0 }; */
    }

    void close()
    {
        libpd_closefile(getPointer<t_canvas*>());
    }

    bool isDirty()
    {
        return getPointer<t_canvas*>()->gl_dirty;
    }

    void savePatch(File const& location)
    {
        /*
        String fullPathname = location.getParentDirectory().getFullPathName();
        String filename = location.getFileName();

        auto* dir = gensym(fullPathname.toRawUTF8());
        auto* file = gensym(filename.toRawUTF8());
        libpd_savetofile(getPointer<t_canvas*>(), file, dir);

        setTitle(filename);

        canvas_dirty(getPointer<t_canvas*>(), 0);
        currentFile = location; */
    }

    void savePatch()
    {
        /*
        String fullPathname = currentFile.getParentDirectory().getFullPathName();
        String filename = currentFile.getFileName();

        auto* dir = gensym(fullPathname.toRawUTF8());
        auto* file = gensym(filename.toRawUTF8());

        libpd_savetofile(getPointer<t_canvas*>(), file, dir);

        setTitle(filename);

        canvas_dirty(getPointer<t_canvas*>(), 0); */
    }

    void setCurrent()
    {
        auto* cnv = canvas_getcurrent();

        if (cnv) {
            canvas_unsetcurrent(cnv);
        }
        
        canvas_setcurrent(getPointer<t_canvas*>());
        canvas_vis(getPointer<t_canvas*>(), 1.);
        canvas_map(getPointer<t_canvas*>(), 1.);
        
        canvas_create_editor(getPointer<t_canvas*>());

        t_atom argv[1];
        SETFLOAT(argv, 1);
        pd_typedmess((t_pd*)getPointer<t_canvas*>(), gensym("pop"), 1, argv);
    }

    int getIndex(void* obj)
    {
        
        int i = 0;
        auto* cnv = getPointer<t_canvas*>();

        for (t_gobj* y = cnv->gl_list; y; y = y->g_next) {
            
            //if (Storage::isInfoParent(y))
            //    continue;

            if (obj == y) {
                return i;
            }

            i++;
        }

        return -1;
    }

    void* createGraphOnParent(int x, int y)
    {
        setCurrent();
        return libpd_creategraphonparent(getPointer<t_canvas*>(), x, y);
    }

    void* createGraph(String const& name, int size, int x, int y)
    {
        setCurrent();
        return libpd_creategraph(getPointer<t_canvas*>(), name.toRawUTF8(), size, x, y);
    }
    
    void createObject(String initialiser) {
        
        auto* cnv = getPointer<t_canvas*>();
        
        auto tokens = StringArray::fromTokens(initialiser, true);
        
        auto* sym = gensym(tokens[0].toRawUTF8());
        tokens.remove(0);
        
        int argc = tokens.size();
        auto argv = std::vector<t_atom>(argc);

        // Set position
        //SETFLOAT(argv.data(), static_cast<float>(x));
        //SETFLOAT(argv.data() + 1, static_cast<float>(y));

        for (int i = 0; i < tokens.size(); i++) {
            auto& tok = tokens[i];
            if (tokens[i].containsOnly("0123456789e.-+") && tokens[i] != "-") {
                SETFLOAT(argv.data() + i, tokens[i].getFloatValue());
            } else {
                SETSYMBOL(argv.data() + i, gensym(tokens[i].toRawUTF8()));
            }
        }
        
        libpd_createobj(cnv, sym, argv.size(), argv.data());
        
        synchronise();
    }

    static int glist_getindex(t_glist* x, t_gobj* y)
    {
        t_gobj* y2;
        int indx;

        for (y2 = x->gl_list, indx = 0; y2 && y2 != y; y2 = y2->g_next)
            indx++;
        return (indx);
    }

    void renameObject(String objID, String const& name)
    {

        auto* obj = getObjectByID(objID);
        
        setCurrent();
        libpd_renameobj(getPointer<t_canvas*>(), &checkObject(obj)->te_g, name.toRawUTF8(), name.getNumBytesAsUTF8());

        // make sure that creating a graph doesn't leave it as the current patch
        setCurrent();
        
        synchronise();

        //return libpd_newest(getPointer<t_canvas*>());
    }

    void copy(std::vector<void*> const& items)
    {
        
        setCurrent();

        glist_noselect(getPointer<t_canvas*>());

        for (auto* obj : items) {
            if (!obj) continue;

            glist_select(getPointer<t_canvas*>(), &checkObject(obj)->te_g);
        }
        
        int size;
        const char* text = libpd_copy(getPointer<t_canvas*>(), &size);
        auto copied = String::fromUTF8(text, size);
        SystemClipboard::copyTextToClipboard(copied);
    }

    void paste()
    {
        auto text = SystemClipboard::getTextFromClipboard();

        libpd_paste(getPointer<t_canvas*>(), text.toRawUTF8());
        
        synchronise();
        sendSelectionToGUI();
    }

    void duplicate(std::vector<void*> const& items)
    {
        setCurrent();
        
        glist_noselect(getPointer<t_canvas*>());

        for (auto* obj : items) {
            if (!obj) continue;

            glist_select(getPointer<t_canvas*>(), &checkObject(obj)->te_g);
        }
        
        libpd_duplicate(getPointer<t_canvas*>());
    
        synchronise();
        sendSelectionToGUI();
    }

    void selectObject(void* obj)
    {
        auto* checked = &checkObject(obj)->te_g;
        if (!glist_isselected(getPointer<t_canvas*>(), checked)) {
            glist_select(getPointer<t_canvas*>(), checked);
        }
    }

    void deselectAll()
    {
        glist_noselect(getPointer<t_canvas*>());
        EDITOR->canvas_undo_already_set_move = 0;
    }

    void removeObject(String objID)
    {
        auto* obj = reinterpret_cast<void*>(objID.getLargeIntValue());
        setCurrent();
        libpd_removeobj(getPointer<t_canvas*>(), &checkObject(obj)->te_g);
        
        synchronise();
    }

    bool hasConnection(void* src, int nout, void* sink, int nin)
    {
        return libpd_hasconnection(getPointer<t_canvas*>(), checkObject(src), nout, checkObject(sink), nin);
    }

    bool canConnect(void* src, int nout, void* sink, int nin)
    {
        return libpd_canconnect(getPointer<t_canvas*>(), checkObject(src), nout, checkObject(sink), nin);
    }

    bool createConnection(String srcID, int nout, String sinkID, int nin)
    {
        
        void* src = getObjectByID(srcID);
        void* sink = getObjectByID(sinkID);
        
        if(!src || !sink) return false;
        
        setCurrent();
        
        if (!libpd_canconnect(getPointer<t_canvas*>(), checkObject(src), nout, checkObject(sink), nin))
            return false;

        libpd_createconnection(getPointer<t_canvas*>(), checkObject(src), nout, checkObject(sink), nin);
        
        synchronise();
        
        return true;
    }

    void removeConnection(String srcID, int nout, String sinkID, int nin)
    {
        void* src = getObjectByID(srcID);
        void* sink = getObjectByID(sinkID);
        
        if(!src || !sink) return;
        
        setCurrent();

        libpd_removeconnection(getPointer<t_canvas*>(), checkObject(src), nout, checkObject(sink), nin);
        
        synchronise();
    }

    void moveObjects(std::vector<void*> const& items, int dx, int dy)
    {
        setCurrent();

        glist_noselect(getPointer<t_canvas*>());

        for (auto* obj : items) {
            if (!obj)
                continue;

            glist_select(getPointer<t_canvas*>(), &checkObject(obj)->te_g);
        }

        libpd_moveselection(getPointer<t_canvas*>(), dx, dy);

        glist_noselect(getPointer<t_canvas*>());
        EDITOR->canvas_undo_already_set_move = 0;
        setCurrent();
    }

    void finishRemove()
    {
        setCurrent();
        libpd_finishremove(getPointer<t_canvas*>());
    }

    void removeObjects(std::vector<void*> const& items)
    {
        setCurrent();
        
        glist_noselect(getPointer<t_canvas*>());

        for (auto* obj : items) {
            if (!obj)
                continue;

            glist_select(getPointer<t_canvas*>(), &checkObject(obj)->te_g);
        }


        libpd_removeselection(getPointer<t_canvas*>());
        
        synchronise();
    }

    void startUndoSequence(String name)
    {
        canvas_undo_add(getPointer<t_canvas*>(), UNDO_SEQUENCE_START, name.toRawUTF8(), 0);
    }

    void endUndoSequence(String name)
    {
        canvas_undo_add(getPointer<t_canvas*>(), UNDO_SEQUENCE_END, name.toRawUTF8(), 0);
    }

    void undo()
    {
        setCurrent();
        glist_noselect(getPointer<t_canvas*>());
        EDITOR->canvas_undo_already_set_move = 0;

        libpd_undo(getPointer<t_canvas*>());

        setCurrent();
        synchronise();
    }

    void redo()
    {
        setCurrent();
        glist_noselect(getPointer<t_canvas*>());
        EDITOR->canvas_undo_already_set_move = 0;

        libpd_redo(getPointer<t_canvas*>());

        setCurrent();
        synchronise();
    }

    void setZoom(int newZoom)
    {
        t_atom arg;
        SETFLOAT(&arg, newZoom);

        pd_typedmess(getPointer<t_pd*>(), gensym("zoom"), 2, &arg);
    }

    t_object* checkObject(void* obj)
    {
        return pd_checkobject(static_cast<t_pd*>(obj));
    }

    void keyPress(int keycode, int shift)
    {
        
        t_atom args[3];

        SETFLOAT(args, 1);
        SETFLOAT(args + 1, keycode);
        SETFLOAT(args + 2, shift);

        pd_typedmess(getPointer<t_pd*>(), gensym("key"), 3, args);
        
    }

    String getTitle()
    {
        String name = String::fromUTF8(getPointer<t_canvas*>()->gl_name->s_name);
        return name.isEmpty() ? "Untitled Patcher" : name;
    }

    void setTitle(String const& title)
    {
        canvas_unbind(getPointer<t_canvas*>());
        getPointer<t_canvas*>()->gl_name = gensym(title.toRawUTF8());
        canvas_bind(getPointer<t_canvas*>());
    }
    
    void sendSelectionToGUI()
    {
        StringArray selection;
        for (auto& object : objects)
        {
            if (glist_isselected(getPointer<t_glist*>(), object.getPointer<t_gobj*>()))
            {
                selection.add(object.getID());
            }
        }
        
        MemoryOutputStream message;
        message.writeInt(MessageHandler::tPatch);
        message.writeString(getID());
        
        message.writeString("Select");

        message.writeString("#");
        for(auto& ID : selection) {
            message.writeString(ID);
        }
        message.writeString("#");
        
        messageHandler.sendMessage(message.getMemoryBlock());
    }
    
    void receiveMessage(MemoryBlock message) {
        MemoryInputStream istream(message, false);
        auto selector = istream.readString();
        if(selector == "CreateObject") {
            auto initializer = istream.readString();
            createObject(initializer);
        }
        if(selector == "RenameObject") {
            auto objectID = istream.readString();
            auto newname = istream.readString();
            renameObject(objectID, newname);
        }
        if(selector == "RemoveSelection") {            
            // Start of selection
            jassert(istream.readString() == "#");
            
            std::vector<void*> objects;
            
            while(!istream.isExhausted())  {
                auto itemID = istream.readString();
                
                // End of selection
                if(itemID == "#") break;
                
                objects.push_back(getObjectByID(itemID));
            }
            
            removeObjects(objects);
        }
        if(selector == "MoveSelection") {
            
            int dx = istream.readInt();
            int dy = istream.readInt();
            
            // Start of selection
            jassert(istream.readString() == "#");
            
            std::vector<void*> objects;
            
            while(!istream.isExhausted())  {
                auto itemID = istream.readString();
                
                // End of selection
                if(itemID == "#") break;
                
                objects.push_back(getObjectByID(itemID));
            }
            
            moveObjects(objects, dx, dy);
            
            synchronise();
            //removeObjects(objects);
        }
        if(selector == "CreateConnection") {
            auto outIdx = istream.readInt();
            auto outObj = istream.readString();
            auto inIdx = istream.readInt();
            auto inObj = istream.readString();

            createConnection(outObj, outIdx, inObj, inIdx);
        }
        if(selector == "RemoveConnection") {
            auto outIdx = istream.readInt();
            auto outObj = istream.readString();
            auto inIdx = istream.readInt();
            auto inObj = istream.readString();

            removeConnection(outObj, outIdx, inObj, inIdx);
        }
        if(selector == "Copy") {
            
            // Start of selection
            jassert(istream.readString() == "#");
            
            std::vector<void*> objects;
            
            while(!istream.isExhausted())  {
                auto itemID = istream.readString();
                
                // End of selection
                if(itemID == "#") break;
                
                objects.push_back(getObjectByID(itemID));
            }
            
            copy(objects);
        }
        if(selector == "Paste") {
            paste();
        }
        if(selector == "Duplicate") {
            
            // Start of selection
            jassert(istream.readString() == "#");
            
            std::vector<void*> objects;
            
            while(!istream.isExhausted())  {
                auto itemID = istream.readString();
                
                // End of selection
                if(itemID == "#") break;
                
                objects.push_back(getObjectByID(itemID));
            }
            
            duplicate(objects);
        }
        if(selector == "Undo") {
            undo();
        }
        if(selector == "Redo") {
            redo();
        }
        if(selector == "Encapsulate") {
        }
        if(selector == "RequestSync") {
            synchronise();
        }
        
    }

    std::vector<Object> objects;
    std::vector<Connection> connections;
};
