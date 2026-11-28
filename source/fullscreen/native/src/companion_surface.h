#ifndef ID6_COMPANION_SURFACE_H
#define ID6_COMPANION_SURFACE_H

#include "surface_session.h"
#include "companions.h"
#include "gauge_core.h"

class CompanionSurface {
    enum { pollKey=0x49443643u, routeCount=160 };
    struct Route {
        CompanionSurface *screen;
        unsigned page, selection;
        bool handle(const bmw::Parcel& data) {
            if (selection) {
                if (screen->active_==page) screen->selected_=selection;
            } else {
                bool focused;
                if (!data.readBool(focused)) return true;
                if (focused) {
                    const companion_page *detail=companion_lookup(ID6_MENU_SURFACE,page);
                    screen->active_=page;
                    screen->selected_=detail && detail->count ? companion_buttons[detail->first].select : 0;
                } else if (screen->active_==page) {
                    screen->active_=0;
                }
            }
            screen->draw();
            screen->schedule();
            return true;
        }
    };
    bmw::PluginApp *app_;
    SurfaceSession renderer_;
    Route routes_[routeCount];
    unsigned used_, active_, selected_;
    bool queued_, visible_;
    char models_[80][96];
    char mainStatus_[96];

    void draw() {
        renderer_.reap();
        if (!active_ && !visible_) return;
        surface_state state;
        memset(&state,0,sizeof state);
        state.magic=surface_magic;
        state.page=active_ ? ID6_MENU_SURFACE : surface_hidden;
        state.selection=active_ ? selected_ : ID6_MENU_SURFACE;
        state.launch_mode=active_;
        if (active_) {
            const companion_page *page=companion_lookup(ID6_MENU_SURFACE,active_);
            if (page && page->status_model<80)
                snprintf(state.status,sizeof state.status,"%s",models_[page->status_model]);
            if (ID6_MENU_SURFACE==surface_mg && active_==100 && mainStatus_[0])
                snprintf(state.status,sizeof state.status,"%s",mainStatus_);
            if (ID6_MENU_SURFACE==surface_hybrid) {
                for (unsigned i=0;i<8;++i) {
                    const char *value=models_[50+i];
                    snprintf(state.values[i],sizeof state.values[i],"%.*s",
                             static_cast<int>(sizeof state.values[i])-1,value);
                }
            }
            if (!renderer_.open("/var/opt/hmi/data/ID5/Plugins/ID6LibraryLab")) return;
        }
        visible_=renderer_.sendState(state,bd_mono_ms()) && active_;
    }

    void schedule() {
        if (!app_ || !active_ || queued_) return;
        queued_=true;
        bmw::Parcel data;
        data.writeUInt32(pollKey);
        app_->post(bmw::Message(0x40000003u,data),60u);
    }

    void registerRoute(unsigned page, unsigned selection) {
        if (used_==routeCount) return;
        Route& route=routes_[used_++];
        route.screen=this;
        route.page=page;
        route.selection=selection;
        typedef bmw::Delegate<bool,const bmw::Parcel&> Handler;
        Handler action;
        action.m_obj=&route;
        action.m_exec=&Handler::execImpl<Route,&Route::handle>;
        if (selection) app_->addHandler(selection,action);
        else app_->addHandler(page,1u,action);
    }

public:
    CompanionSurface() : app_(0),used_(0),active_(0),selected_(0),queued_(false),visible_(false) {
        memset(models_,0,sizeof models_);
        mainStatus_[0]=0;
    }

    void bind(bmw::PluginApp& owner) {
        app_=&owner;
        used_=0;
        for (unsigned i=0;i<sizeof companion_pages/sizeof companion_pages[0];++i) {
            const companion_page& page=companion_pages[i];
            if (page.kind!=ID6_MENU_SURFACE) continue;
            registerRoute(page.id,0);
            for (unsigned offset=0;offset<page.count;++offset)
                registerRoute(page.id,companion_buttons[page.first+offset].select);
        }
    }

    void update(unsigned model, const char *value) {
        if (model>=80 || !strcmp(models_[model],value)) return;
        snprintf(models_[model],sizeof models_[model],"%s",value);
        for (char *p=models_[model];*p;++p)
            if (*p=='\r' || *p=='\n') *p=' ';
        if (ID6_MENU_SURFACE==surface_mg && (model==50 || model==51))
            snprintf(mainStatus_,sizeof mainStatus_,"%s",models_[model]);
        draw();
    }

    bool poll(unsigned key) {
        if (key!=pollKey) return false;
        queued_=false;
        draw();
        schedule();
        return true;
    }

    void close() {
        active_=0;
        draw();
        renderer_.closeChannel();
    }
};

#endif
