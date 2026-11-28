#include "plugin_factory.h"
#include "diagnostic_gateway.h"
#include "diagnostic_worker.h"
#include "dct_settings.h"
#include "warm_units.h"
#include "drive_frame.h"
#include "boost_layout.h"
#include "boost_files.h"
#include "boost_settings_assets.h"
#include "gauge_core.h"
#include "warm_frame.h"
#include "road_jet_frame.h"
#include "launch_records.h"
#include "surface_session.h"
#include "warm_state.h"
#include "shift_state.h"
#include "gauge_coexistence.h"
#ifdef ID6_EMULATOR
#include "emulator_telemetry.h"
#include "surface_assets.h"
#endif
#include <arpa/inet.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/wait.h>

namespace {
enum { boost = surface_boost, shift = surface_shift, mhd = surface_mhd, codes = surface_codes,
       road = surface_road, beamer = surface_beamer, launchPage = surface_launch,
       warm = surface_warm, home = surface_home,
       updates = surface_updates, systemPage = surface_system,
       updateConfirm = surface_update_confirm, installConfirm = surface_install_confirm, inactive = surface_hidden,
       boostSettings, shiftSettings, codesConfirm, shiftGasSettings, poll_key = 0x4944364cu };
enum { live_poll_ms = 16, menu_poll_ms = 40 };
const unsigned kEntryPage = ID6_ENTRY_PAGE;
enum { shift_target_count=ShiftPreferences::GASOLINE_TARGETS };
#ifdef ID6_EMULATOR
const char kShiftPreferences[] = "/dev/shmem/ID6FullShiftDeck.prefs";
const char kShiftProfile[] = "/dev/shmem/ID6FullShiftDeck.diesel.prefs";
#else
const char kShiftPreferences[] = "/var/opt/hmi/data/ID5/ID6FullShiftDeck.prefs";
const char kShiftProfile[] = "/var/opt/hmi/data/ID5/ID6FullShiftDeck.diesel.prefs";
#endif
const char kLaunchPreferences[] = "/var/opt/hmi/data/ID5/ID6LibraryLab.launch.prefs";
const char kBoostPrefs[] = ID6_BOOST_PREFS;
const char kBoostLayout[] = ID6_BOOST_LAYOUT;
const unsigned kAlertSignals[] = {BD_COOLANT, BD_OIL_TEMP, BD_OIL};
const char kRoot[] = "/var/opt/hmi/data/ID5/Plugins/ID6LibraryLab";
const char *worker(unsigned page) {
    switch (page) {
        case road: return "road-jet-qnx";
        case boost: return "boostdeck-collector-qnx";
        case shift: case launchPage: return "shift-deck-qnx";
        default: return "warmdeck-collector-qnx";
    }
}
unsigned port(unsigned page) {
    if (page == road) return ROAD_JET_PORT;
    return page == boost ? 42910u : page == shift ? 42911u : page == launchPage ? 42913u : 42912u;
}
unsigned fields(unsigned page) {
    return page == boost ? 9u : page == shift ? 2u : page == launchPage ? 5u : 3u;
}
bool usbPage(unsigned page) { return page == updates || page == updateConfirm || page == installConfirm; }
bool usesWorker(unsigned page) { return page == boost || page == shift || page == launchPage || page == warm || page == road; }
bool fresh(uint64_t now, uint64_t sample, unsigned age) {
    return sample && sample <= now && now - sample <= age;
}
}

class ID6LibraryLab : public bmw::PluginApp {
    unsigned page_, workerPage_;
    int socket_;
    pid_t child_;
    bool queued_, childStopping_, received_, telemetryFailed_;
    uint64_t lastPacket_;
    char last_[9][surface_text_size];
    char surfaceStatus_[96];
    BoostPreferences boostPrefs_;
    BoostLayout boostLayout_;
    BoostAlerts boostAlerts_;
    uint16_t boostLevels_[9];
#ifdef ID6_EMULATOR
    unsigned boostFiles_;
#endif
    unsigned boostSlot_;
    pid_t tone_;
    uint64_t lastTone_;
    SurfaceSession surface_;
    FullShiftPreferences shiftPrefs_;
    FullShiftState shiftState_;
    const char *shiftPreferences_, *shiftProfile_;
    bool shiftSaved_;
    bool warmFahrenheit_;
    FullWarmState warmState_;
    launch_timer timer_;
    lt_mode launchMode_;
    LaunchRecords records_;
    bool surfaceFailed_;
    bool codesStarted_;
    unsigned codeSize_;
    unsigned codeOffset_;
    char codeBuffer_[3072];
    unsigned controlFocus_, lamps_, map_, controlUsed_;
    int cooling_;
    char controlReply_[128];
    pid_t usbChild_;
    int usbRead_;
    unsigned usbUsed_;
    char usbReply_[96];
    uint64_t roadSample_;
    unsigned roadMode_, roadDistance_, roadLane_, roadAltitude_;
    bool roadHasScene_, codesClearPending_;

    void loadBoostConfiguration() {
        boostPrefs_.load(access(kBoostPrefs,F_OK)==0 ? kBoostPrefs :
                         "/var/opt/hmi/data/ID5/BoostDeck.prefs",9);
        if (!boostLayout_.load(kBoostLayout))
            boostLayout_.load("/var/opt/hmi/data/ID5/BoostDeck.signals");
    }

    void refreshBoostSettings() {
        char label[72];
        for (unsigned i=0;i<10;++i) {
            const unsigned choice=i<BOOST_SETTING_COUNT ? boostPrefs_.choice[i] :
                                  i==BOOST_SETTING_COUNT ? boostPrefs_.color : boostLayout_.diesel;
            snprintf(label,sizeof label,"%s: %s",boost_option_names[i],boost_option_values[i][choice]);
            updateModel(1800u+i,bmw::BasicString<char>(label));
        }
        for (unsigned i=0;i<9;++i) {
            snprintf(label,sizeof label,"Gauge %u: %s",i+1,bd_name(boostLayout_.signal[boostLayout_.diesel][i]));
            updateModel(1820u+i,bmw::BasicString<char>(label));
        }
    }

    bool boostSaveError() {
        updateModel(1899u,bmw::BasicString<char>("Could not save BoostDeck settings"));
        return true;
    }

    template<unsigned Setting, unsigned Choice> bool boostOption(const bmw::Parcel&) {
        return changeBoostOption(Setting,Choice);
    }

    bool changeBoostOption(unsigned setting,unsigned choice) {
        if (setting==9) {
            BoostLayout next=boostLayout_;
            next.diesel=choice!=0;
            if (!next.save(kBoostLayout)) return boostSaveError();
            boostLayout_=next;
        } else {
            BoostPreferences next=boostPrefs_;
            if (setting==8) next.color=choice;
            else next.choice[setting]=choice;
            if (!next.save(kBoostPrefs)) return boostSaveError();
            boostPrefs_=next;
        }
        updateModel(1899u,bmw::BasicString<char>(""));
        refreshBoostSettings();
        return true;
    }

    template<unsigned Slot> bool boostPicker(const bmw::Parcel&) {
        boostSlot_=Slot;
        char title[64];
        snprintf(title,sizeof title,"Gauge %u: %s",Slot+1,bd_name(boostLayout_.signal[boostLayout_.diesel][Slot]));
        updateModel(1898u,bmw::BasicString<char>(title));
        return true;
    }

    template<unsigned Signal> bool boostGauge(const bmw::Parcel&) {
        if (boostSlot_>=9) return true;
        BoostLayout next=boostLayout_;
        next.signal[next.diesel][boostSlot_]=Signal;
        if (!next.save(kBoostLayout)) return boostSaveError();
        boostLayout_=next;
        updateModel(1899u,bmw::BasicString<char>(""));
        refreshBoostSettings();
        return true;
    }

    void reapTone() {
        if (tone_<=0) return;
        const pid_t ended=waitpid(tone_,0,WNOHANG);
        if (ended==tone_ || (ended<0 && errno==ECHILD)) tone_=-1;
    }

    void checkBoostAlerts(const bd_frame& data, uint64_t now) {
        static const char* const warnings[]={"Coolant high","Oil temperature high","Oil pressure low"};
        const uint64_t elapsed=now-data.sent_ms;
        bool chime=false;
        char warning[96]="";
        for (unsigned i=0;i<3;++i) {
            const unsigned option=boostPrefs_.choice[BOOST_COOLANT_ALERT+i];
            const unsigned signal=kAlertSignals[i];
            const bool rpmValid=bd_is_fresh(&data,BD_RPM,elapsed);
            if (i==2 && rpmValid && data.value[BD_RPM]<600) boostAlerts_.reset(i);
            const bool valid=bd_is_fresh(&data,signal,elapsed) &&
                             (i!=2 || (rpmValid && data.value[BD_RPM]>=600));
            const uint64_t sampled=valid && data.sent_ms>data.age_ms[signal] ?
                                   data.sent_ms-data.age_ms[signal] : 0;
            bool entered=false;
            if (boostAlerts_.evaluate(i,option!=0,sampled!=0,data.value[signal],
                                      option ? boostAlertThreshold(i,option) : 0,sampled,entered)) {
                if (*warning) strcat(warning,"  /  ");
                strcat(warning,warnings[i]);
            }
            chime=chime || entered;
        }
#ifdef ID6_EMULATOR
        status(boost,*warning ? warning : "IDEmu simulated telemetry");
#else
        status(boost,*warning ? warning : "");
        if (chime && !boostPrefs_.choice[BOOST_CHIME] && tone_<0 &&
            (!lastTone_ || now-lastTone_>=12000)) {
            char path[176];
            snprintf(path,sizeof path,"%s/boostdeck-collector-qnx",kRoot);
            const char* args[]={path,"--alert-tone",0};
            tone_=launch(args,-1);
            lastTone_=now;
        }
#endif
        (void)chime;
        (void)lastTone_;
    }

    void showMhdSettings() {
        char text[36], map[9] = "--";
        if (map_) snprintf(map,sizeof map,"%u",map_);
        snprintf(text,sizeof text,"Map %s  |  Max cooling %s",map,
                 cooling_ < 0 ? "--" : cooling_ ? "on" : "off");
        display(mhd,0,text);
    }
    bool startControl(const char* executable, const char* mode, unsigned value) {
        reap();
        if (child_ > 0 || socket_ >= 0) return false;
        int ends[2];
        if (pipe(ends) < 0) { status(page_,"Cannot open diagnostics"); return false; }
        char path[176], gatewayPath[176], gateway[16] = "auto";
        snprintf(path,sizeof path,"%s/%s",kRoot,executable);
        snprintf(gatewayPath,sizeof gatewayPath,"%s/gateway-ip.txt",kRoot);
        if (!readDiagnosticGateway(gatewayPath,gateway)) readDiagnosticGateway(ID6_GATEWAY_SETTINGS,gateway);
        const char argument[] = {static_cast<char>('0'+value),0};
        const char *args[] = {path,"--host",gateway,mode ? mode : argument,mode ? argument : 0,0};
        if (fcntl(ends[0],F_SETFL,O_NONBLOCK) == 0 &&
            fcntl(ends[0],F_SETFD,FD_CLOEXEC) == 0 && fcntl(ends[1],F_SETFD,FD_CLOEXEC) == 0)
            child_ = launch(args,ends[1]);
        close(ends[1]);
        if (child_ <= 0) {
            close(ends[0]);
            status(page_,"Cannot start diagnostics");
            return false;
        }
        socket_ = ends[0];
        workerPage_ = page_;
        childStopping_ = false;
        controlUsed_ = 0;
        controlReply_[0] = 0;
        status(page_,beamer == page_ ? "Checking lighting module and speed" : "Sending request to DME");
        schedule();
        return true;
    }
    template<unsigned Action> bool mhdAction(const bmw::Parcel&) {
        if (page_ != mhd) return true;
        const unsigned index = Action - 240;
#ifdef ID6_EMULATOR
        previewControl(mhd, mhd_controls[index].title);
#else
        if (index < 4) startControl("mhd-maps-qnx","--map",index+1);
        else if (index < 7) startControl("exhaust-flap-worker-qnx","--flap",index-4);
        else startControl("mhd-maps-qnx","--cooling",index-7);
#endif
        return true;
    }
    template<unsigned Action> bool beamAction(const bmw::Parcel&) {
        if (page_ != beamer) return true;
        if (Action == 254) {
            stopWorker();
            lamps_ = 0;
            status(beamer,"Stopped");
        } else {
#ifdef ID6_EMULATOR
            previewControl(beamer, beamer_controls[Action-250].title);
#else
            startControl("beamer-qnx",0,Action-250);
#endif
        }
        return true;
    }
#ifdef ID6_EMULATOR
    void previewControl(unsigned page, const char* title) {
        char text[64];
        snprintf(text,sizeof text,"IDEmu: %s selected",title);
        status(page,text);
    }
#endif
    template<unsigned Page, unsigned Action> bool selectControl(const bmw::Parcel&) {
        if (page_ == Page && controlFocus_ != Action) {
            controlFocus_ = Action;
            syncSurface();
        }
        return true;
    }
    template<unsigned Page, unsigned Action> void registerSelection() {
        typedef bmw::Delegate<bool,const bmw::Parcel&> Delegate;
        Delegate action;
        action.m_obj=this;
        action.m_exec=&Delegate::execImpl<ID6LibraryLab,&ID6LibraryLab::selectControl<Page,Action> >;
        addHandler(6000+Action,action);
    }
    template<unsigned Page, unsigned Action, bool (ID6LibraryLab::*Run)(const bmw::Parcel&)>
    void registerControl() {
        typedef bmw::Delegate<bool,const bmw::Parcel&> Delegate;
        Delegate action;
        action.m_obj=this;
        action.m_exec=&Delegate::execImpl<ID6LibraryLab,Run>;
        addHandler(Action,action);
        if (Page != updateConfirm && Page != installConfirm) registerSelection<Page,Action>();
    }
    void closeControl() {
        close(socket_);
        socket_ = -1;
    }
    void finishMhd() {
        while (controlUsed_ && (controlReply_[controlUsed_-1] == '\n' || controlReply_[controlUsed_-1] == '\r'))
            --controlUsed_;
        controlReply_[controlUsed_]=0;
        const char prefix[] = "Accepted by DME: Map ";
        if (controlUsed_ == sizeof prefix && !strncmp(controlReply_,prefix,sizeof prefix-1) &&
            controlReply_[sizeof prefix-1] >= '1' && controlReply_[sizeof prefix-1] <= '4')
            map_ = unsigned(controlReply_[sizeof prefix-1] - '0');
        if (!strcmp(controlReply_,"Accepted by DME: Max Cooling on")) cooling_ = 1;
        if (!strcmp(controlReply_,"Accepted by DME: Max Cooling off")) cooling_ = 0;
        showMhdSettings();
        status(mhd,controlUsed_ ? controlReply_ : "No result from diagnostics");
    }
    void beamStatus(unsigned char signal) {
        if (signal <= 15) { lamps_ = signal; return; }
        lamps_ = 0;
        const char* message;
        switch (signal) {
            case 'S': status(beamer,"Playing exterior light pattern"); return;
            case 'D': message="Pattern finished"; break;
            case 'M': message="Slow to 5 mph / 8 km/h or less"; break;
            case 'V': message="Vehicle speed unavailable"; break;
            case 'K': message="Pattern finished. Unsupported lamps skipped."; break;
            case 'L': message="Selected lamps are unsupported"; break;
            case 'E': message="Lighting diagnostic session unavailable"; break;
            case 'U': message="Lighting module profile unavailable"; break;
            case 'B': message="Diagnostics busy. Try again."; break;
            case 'G': message="Lighting gateway unavailable"; break;
            default: message="Lighting module did not confirm the pulse"; break;
        }
        status(beamer,message);
        closeControl();
    }
    void pollControl() {
        if (socket_ < 0 || workerPage_ != page_) return;
        if (page_ == mhd) {
            ssize_t n = read(socket_,controlReply_+controlUsed_,sizeof controlReply_-controlUsed_-1);
            if (n > 0) { controlUsed_ += unsigned(n); return; }
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) return;
            closeControl();
            finishMhd();
            return;
        }
        for (unsigned i=0; i<24 && socket_>=0; ++i) {
            unsigned char signal;
            ssize_t n = read(socket_,&signal,1);
            if (n == 1) beamStatus(signal);
            else {
                if (!n || (errno != EINTR && errno != EAGAIN)) {
                    lamps_ = 0;
                    status(beamer,"Lighting connection ended");
                    closeControl();
                }
                break;
            }
        }
    }
    template<unsigned Action> bool usbAction(const bmw::Parcel&) {
        const unsigned expected = Action == 261 ? installConfirm : updateConfirm;
        if (page_ != expected || usbChild_ > 0 || usbRead_ >= 0) return true;
        int ends[2];
        if (pipe(ends) < 0) { status(page_,"Could not open the USB worker channel."); return true; }
        char path[176];
        snprintf(path,sizeof path,"%s/library-usb-update-qnx",kRoot);
        const char *args[] = {path, Action == 261 ? "--install-new" : "--update", 0};
        if (fcntl(ends[0],F_SETFL,O_NONBLOCK) == 0 &&
            fcntl(ends[0],F_SETFD,FD_CLOEXEC) == 0 && fcntl(ends[1],F_SETFD,FD_CLOEXEC) == 0)
            usbChild_ = launch(args,ends[1]);
        close(ends[1]);
        if (usbChild_ <= 0) {
            close(ends[0]);
            status(page_,"Could not start the USB worker.");
            return true;
        }
        usbRead_ = ends[0];
        usbUsed_ = 0;
        usbReply_[0] = 0;
        status(page_,"Checking the signed USB package...");
        schedule();
        return true;
    }

    void pollUsb() {
        if (usbChild_ > 0) {
            pid_t exited = waitpid(usbChild_,0,WNOHANG);
            if (exited == usbChild_ || (exited < 0 && errno == ECHILD)) usbChild_ = -1;
        }
        if (usbRead_ < 0) return;
        char discard[64];
        char *destination = usbUsed_ < sizeof usbReply_-1 ? usbReply_+usbUsed_ : discard;
        const unsigned room = destination == discard ? sizeof discard : sizeof usbReply_-1-usbUsed_;
        const ssize_t n = read(usbRead_,destination,room);
        if (n > 0) {
            if (destination != discard) usbUsed_ += unsigned(n);
            return;
        }
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) return;
        close(usbRead_);
        usbRead_ = -1;
        while (usbUsed_ && (usbReply_[usbUsed_-1] == '\n' || usbReply_[usbUsed_-1] == '\r'))
            --usbUsed_;
        usbReply_[usbUsed_] = 0;
        if (!usbUsed_) snprintf(usbReply_,sizeof usbReply_,"USB worker ended without a result.");
        if (usbPage(page_)) status(page_,usbReply_);
    }

    void setRoadMode(unsigned mode) {
        if (roadMode_ == mode) return;
        roadMode_ = mode;
        switch (mode) {
            case ROAD_JET_ACTIVE: status(road,"Pedal climbs. Steering changes lanes."); break;
            case ROAD_JET_PEDAL_ONLY: status(road,"Pedal only. Steering data unavailable."); break;
            case ROAD_JET_TOO_FAST: status(road,"Slow to 5 mph / 8 km/h or less to play."); break;
            default: status(road,roadHasScene_ ? "Paused. Checking vehicle data." : "Waiting for vehicle data"); break;
        }
    }
    void updateRoad(const road_jet_frame& frame, uint64_t now) {
        if (!fresh(now,frame.started_ms,ROAD_JET_STALE_MS) ||
            frame.started_ms <= roadSample_ || frame.pedal < 0 || frame.pedal > 1000 ||
            frame.steering < -90000 || frame.steering > 90000) return;
        unsigned step = roadSample_ && frame.started_ms-roadSample_ < 300 ?
            unsigned(frame.started_ms-roadSample_) : 100;
        roadSample_ = frame.started_ms;
        lastPacket_ = now;
        if (frame.mode == ROAD_JET_TOO_FAST) { setRoadMode(ROAD_JET_TOO_FAST); return; }
        if (frame.mode != ROAD_JET_ACTIVE && frame.mode != ROAD_JET_PEDAL_ONLY) {
            setRoadMode(ROAD_JET_WAITING);
            return;
        }
        int lane = int(roadLane_);
        if (frame.mode == ROAD_JET_ACTIVE) {
            while (lane < 4 && frame.steering >= (lane-2)*3000+1800) ++lane;
            while (lane > 0 && frame.steering <= (lane-2)*3000-1800) --lane;
        }
        roadLane_ = unsigned(lane);
        roadAltitude_ = (roadAltitude_*2+unsigned(frame.pedal)+1)/3;
        roadHasScene_ = true;
        if (!frame.pedal && roadAltitude_ <= 2) roadAltitude_=0;
        roadDistance_ = (roadDistance_ + step*(72+unsigned(frame.pedal)*220/1000)) % 3600000u;
        setRoadMode(unsigned(frame.mode));
    }

    void syncSurface() {
        surface_state state;
        memset(&state,0,sizeof state);
        state.magic = surface_magic;
        state.page = page_;
        state.selection = controlFocus_;
        state.lamps = lamps_;
        state.road_mode = roadMode_;
        state.road_distance = roadDistance_;
        state.road_lane = roadLane_;
        state.road_altitude = roadAltitude_;
        state.road_has_scene = roadHasScene_;
        state.shift_target = shiftPrefs_.valid() ? shiftPrefs_.targetRpm() : 0;
        state.warm_fahrenheit = warmFahrenheit_;
        state.launch_mode = launchMode_;
        if (page_ < home || usbPage(page_)) {
            snprintf(state.status,sizeof state.status,"%s",surfaceStatus_);
            for (unsigned i=0;i<9;++i)
                snprintf(state.values[i],sizeof state.values[i],"%s",last_[i]);
        }
        if (page_ == boost) {
            memcpy(state.boost_signal,boostLayout_.signal[boostLayout_.diesel],sizeof state.boost_signal);
            memcpy(state.boost_level,boostLevels_,sizeof state.boost_level);
            state.boost_color=boostPrefs_.color;
            state.boost_style=boostPrefs_.choice[BOOST_STYLE];
        }
        if (page_ == warm) {
            for (unsigned i=0;i<WARM_COUNT;++i) {
                state.boost_signal[i]=static_cast<uint8_t>(warmState_.target(i));
                state.boost_level[i]=static_cast<uint16_t>(warmState_.level(i));
            }
        }
        if (page_ == shift && shiftPrefs_.valid()) {
            const int rpm=shiftState_.rpm(bd_mono_ms());
            state.boost_level[0]=static_cast<uint16_t>(shiftPrefs_.scaleRpm());
            state.boost_level[1]=rpm>=0 ? static_cast<uint16_t>(rpm+1) : 0;
            state.boost_signal[0]=shiftPrefs_.diesel ? 1 : 0;
            state.boost_signal[1]=shiftSaved_ ? 0 : 1;
        }
        if (page_ == launchPage) state.launch_run_state=timer_.state;
        if (page_==codesConfirm) {
            state.page=codes;
        } else if (page_ >= surface_hidden) {
            state.page=surface_hidden;
            state.selection=kEntryPage;
            surface_.sendState(state, bd_mono_ms());
            return;
        }
        if (!surfaceFailed_) {
            if (!surface_.open(kRoot) || !surface_.sendState(state, bd_mono_ms())) {
                surfaceFailed_ = true;
                if (page_ < home || usbPage(page_)) status(page_,"Full-screen layer unavailable");
            }
        }
    }

    void status(unsigned page, const char *text) {
        if (!strcmp(surfaceStatus_,text)) return;
        snprintf(surfaceStatus_,sizeof surfaceStatus_,"%s",text);
        updateModel(600u + page, bmw::BasicString<char>(text));
    }
    void display(unsigned page, unsigned index, const char *text) {
        if (!strcmp(last_[index], text)) return;
        snprintf(last_[index], sizeof last_[index], "%s", text);
        updateModel(1400u + page * 20u + index, bmw::BasicString<char>(text));
    }
    void refreshWarm(uint64_t now) {
        warmState_.update(now);
        char label[80];
        for (unsigned i=0;i<WARM_COUNT;++i) {
            const unsigned target=warmState_.target(i);
            if (target==surface_warm_unknown) strcpy(label,"--");
            else snprintf(label,sizeof label,"%.1f %c",
                          warm_display_temperature(warmState_.celsius(i),warmFahrenheit_),
                          warmFahrenheit_ ? 'F' : 'C');
            display(warm,i,label);
            snprintf(label,sizeof label,"%.0f %c target: %s",
                     warm_display_temperature(kWarmTarget[i],warmFahrenheit_),warmFahrenheit_ ? 'F' : 'C',
                     target==surface_warm_unknown ? "unknown" : target==surface_warm_met ? "met" : "pending");
            display(warm,i+3,label);
        }
        if (telemetryFailed_ || surfaceFailed_) return;
        const unsigned valid=warmState_.valid();
        const int eta=warmState_.eta();
        if (!valid) strcpy(label,"Waiting for temperature readings");
        else if (valid!=(1u<<WARM_COUNT)-1) strcpy(label,"Waiting for all three temperatures");
        else if (!eta) strcpy(label,"App temperature targets met");
        else if (eta<0) strcpy(label,"Measuring warm-up trend");
        else snprintf(label,sizeof label,"Estimated time to app targets  %d:%02d",eta/60,eta%60);
#ifdef ID6_EMULATOR
        char simulated[96];
        snprintf(simulated,sizeof simulated,"IDEmu simulated: %.78s",label);
        status(warm,simulated);
#else
        status(warm,label);
#endif
    }
    void refreshShift(uint64_t now) {
        char value[36];
        const int rpm=shiftState_.rpm(now), kph=shiftState_.kph(now);
        if (kph>=0) snprintf(value,sizeof value,"%d %s",
            shiftPrefs_.metric ? kph : (kph*621371+500000)/1000000,shiftPrefs_.metric ? "km/h" : "mph");
        else strcpy(value,"--");
        display(shift,0,value);
        if (rpm>=0) snprintf(value,sizeof value,"%d rpm",rpm);
        else strcpy(value,"--");
        display(shift,1,value);
    }
    void clear(unsigned page) {
        memset(last_, 0, sizeof last_);
        if (page==shift) {
            shiftState_.reset(bd_mono_ms());
            refreshShift(bd_mono_ms());
            display(shift,2,"--");
            return;
        }
        if (page==warm) {
            warmState_.reset(bd_mono_ms());
            refreshWarm(bd_mono_ms());
            return;
        }
        for (unsigned i = 0; i < fields(page); ++i) display(page, i, "--");
    }
    void reap() {
        if (child_ < 0) return;
        pid_t exited = waitpid(child_, 0, WNOHANG);
        if (exited == child_ || (exited < 0 && errno == ECHILD)) child_ = -1;
    }
    void stopWorker() {
        if (socket_ >= 0) { close(socket_); socket_ = -1; }
        reap();
        if (child_ > 0 && !childStopping_) {
            kill(child_, SIGTERM);
            childStopping_ = true;
        }
    }
    bool changeDctTemperature(const bmw::Parcel&) {
        const bool saved=toggleDctTemperature();
        if (saved && (page_==boost || page_==warm)) {
            stopWorker();
            clear(page_);
            received_=false;
        }
        updateModel(1880u,bmw::BasicString<char>(saved ? dctTemperatureLabel() : "Could not save DCT setting"));
        return true;
    }
    bool toggleWarmUnits(const bmw::Parcel&) {
        const bool saved=saveWarmFahrenheit(!warmFahrenheit_);
        if (saved) warmFahrenheit_=!warmFahrenheit_;
        updateModel(1882u,bmw::BasicString<char>(saved ? warmUnitsLabel(warmFahrenheit_) :
                                                "Could not save temperature units"));
        if (saved && page_==warm) {
            refreshWarm(bd_mono_ms());
            syncSurface();
        }
        return true;
    }
    void schedule() {
        if (queued_) return;
        queued_ = true;
        bmw::Parcel event;
        event.writeUInt32(poll_key);
        post(bmw::Message(0x40000003u, event), usesWorker(page_) ? live_poll_ms : menu_poll_ms);
    }
    void finishPoll() {
        if (page_==warm) refreshWarm(bd_mono_ms());
        if (page_==shift) refreshShift(bd_mono_ms());
        syncSurface();
        schedule();
    }
    bool startWorker() {
        socket_ = socket(AF_INET, SOCK_DGRAM, 0);
        struct sockaddr_in address;
        memset(&address, 0, sizeof address);
#ifdef __QNX__
        address.sin_len = sizeof address;
#endif
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port(page_));
        if (socket_ < 0 || fcntl(socket_, F_SETFL, O_NONBLOCK) < 0 ||
            fcntl(socket_, F_SETFD, FD_CLOEXEC) < 0 ||
            bind(socket_, reinterpret_cast<const struct sockaddr*>(&address), sizeof address) < 0) {
            stopWorker();
            return false;
        }
        char path[176], gatewayPath[176], udp[12], mask[16];
        snprintf(path, sizeof path, "%s/%s", kRoot, worker(page_));
        snprintf(gatewayPath, sizeof gatewayPath, "%s/gateway-ip.txt", kRoot);
        snprintf(udp, sizeof udp, "%u", port(page_));
        char gateway[16] = "auto";
        if (!readDiagnosticGateway(gatewayPath, gateway))
            readDiagnosticGateway(ID6_GATEWAY_SETTINGS, gateway);
        unsigned selected=0;
        for (unsigned i=0;i<9;++i) selected|=1u<<boostLayout_.signal[boostLayout_.diesel][i];
        for (unsigned i=0;i<3;++i)
            if (boostPrefs_.choice[BOOST_COOLANT_ALERT+i]) selected|=1u<<kAlertSignals[i];
        if (boostPrefs_.choice[BOOST_OIL_PRESSURE_ALERT]) selected|=1u<<BD_RPM;
        snprintf(mask,sizeof mask,"%u",selected);
        const char *base[] = {path, "--host", gateway, "--udp-port", udp, 0};
        const char *launchArgs[] = {path, "--host", gateway, "--udp-port", udp, "--speed-only", 0};
        const char *roadArgs[] = {path, "--host", gateway, 0};
        const char *boostArgs[] = {path, "--host", gateway, "--udp-port", udp,
                                  "--session", "--signals", mask, 0, 0, 0};
        unsigned next=8;
        if (selected&(1u<<BD_ETHANOL)) boostArgs[next++]="--ethanol";
        if (boostLayout_.diesel) boostArgs[next]="--diesel";
        child_ = launch(page_ == boost ? boostArgs : page_ == launchPage ? launchArgs : page_ == road ? roadArgs : base, -1);
        if (child_ <= 0) { stopWorker(); return false; }
        workerPage_ = page_;
        childStopping_ = false;
        received_ = false;
        lastPacket_ = bd_mono_ms();
        return true;
    }
    void render(const char *bytes, unsigned size, uint64_t now) {
        char value[36];
        if (page_ == road) {
            road_jet_frame frame;
            if (size != sizeof frame) return;
            memcpy(&frame,bytes,sizeof frame);
            updateRoad(frame,now);
            return;
        }
        if (page_ == boost) {
            bd_frame frame;
            if (!bd_parse(bytes, size, &frame) || frame.demo ||
                !fresh(now, frame.sent_ms, BD_STALE_MS)) return;
            for (unsigned i=0;i<9;++i) {
                const unsigned signal=boostLayout_.signal[boostLayout_.diesel][i];
                if (bd_is_fresh(&frame,signal,now-frame.sent_ms)) {
                    const unsigned units=boostPrefs_.units(signal);
                    const char* unit=bd_unit_selected(signal,units);
                    const int digits=signal==BD_BATTERY || signal==BD_LAMBDA_TARGET ? 2 :
                                     signal==BD_RPM || signal==BD_RAIL || signal==BD_ETHANOL ? 0 : 1;
                    snprintf(value,sizeof value,"%.*f%s%s",digits,bd_display_selected(signal,frame.value[signal],units),
                             *unit && *unit!='%' ? " " : "",unit);
                    boostLevels_[i]=bd_bar(signal,frame.value[signal]);
                } else {
                    strcpy(value,"--");
                    boostLevels_[i]=0;
                }
                display(boost,i,value);
            }
            checkBoostAlerts(frame,now);
        } else if (page_ == shift) {
            drive_frame frame;
            if (size!=sizeof frame) return;
            memcpy(&frame,bytes,sizeof frame);
            if (!shiftState_.accept(frame,now)) return;
            refreshShift(now);
        } else if (page_ == launchPage) {
            drive_frame frame;
            if (size != sizeof frame) return;
            memcpy(&frame, bytes, sizeof frame);
            if (!fresh(now, frame.sampled_ms, DRIVE_STALE_MS)) return;
            const bool mph = launchMode_ < LT_0_100;
            if (frame.kph >= 0)
                snprintf(value,sizeof value,"%d %s",mph ? (int(frame.kph)*621371+500000)/1000000 : int(frame.kph),
                         mph ? "mph" : "km/h");
            else strcpy(value, "--");
            display(page_, 0, value);
            if (page_ == launchPage) {
                if (lt_update(&timer_,frame.sampled_ms,frame.kph)==LT_EV_FINISHED) {
                    records_ = withFinishedRun(records_,timer_.result_ms);
                    saveLaunchRecords(recordPath(),records_);
                }
                if (timer_.state==LT_ARMED) strcpy(value,"Ready");
                else lt_format_seconds(lt_elapsed(&timer_,frame.sampled_ms),value,sizeof value);
                display(page_,1,value);
                if (!received_) { status(page_, "Worker connected"); received_ = true; }
                if (records_.last_ms) lt_format_seconds(records_.last_ms,value,sizeof value);
                else strcpy(value,"--");
                display(page_,2,value);
                const uint32_t history[]={records_.previous_ms,records_.third_ms};
                for (unsigned i=0;i<2;++i) {
                    if (history[i]) lt_format_seconds(history[i],value,sizeof value);
                    else strcpy(value,"--");
                    display(page_,i+3,value);
                }
                lastPacket_=now;
                return;
            }
        } else {
            warm_frame frame;
            if (size != sizeof frame) return;
            memcpy(&frame, bytes, sizeof frame);
            warmState_.accept(frame,now);
            refreshWarm(now);
        }
        if (!received_) { if (page_!=boost && page_!=warm) status(page_, "Worker connected"); received_ = true; }
        lastPacket_ = now;
    }
#ifdef ID6_EMULATOR
    void pollEmulator() {
        const uint64_t now = bd_mono_ms();
        emulator_telemetry sample;
        const int fd = open(IDEMU_TELEMETRY_FILE,O_RDONLY);
        const bool got = fd >= 0 && read(fd,&sample,sizeof sample) == sizeof sample;
        if (fd >= 0) close(fd);
        if (!got || !fresh(now,sample.sampled_ms,500)) {
            if (received_) {
                if (page_ == road) setRoadMode(ROAD_JET_WAITING);
                else clear(page_);
                if (page_ == launchPage) resetLaunch();
                received_ = false;
            }
            status(page_,"Waiting for IDEmu telemetry");
            return;
        }
        if (lastPacket_ && sample.sampled_ms <= lastPacket_) return;
        const int rpm = sample.ignition >= 69 ? sample.rpm : 0;
        if (page_ == boost) {
            // IDEmu supplies RPM, pedal and coolant. The remaining gauges use
            // simple test values so the layout can be checked under changing load.
            bd_frame frame;
            memset(&frame,0,sizeof frame);
            frame.sent_ms = sample.sampled_ms;
            frame.valid = (1u<<BD_BOOST)|(1u<<BD_RPM)|(1u<<BD_IAT)|
                          (1u<<BD_COOLANT)|(1u<<BD_OIL)|(1u<<BD_ETHANOL);
            frame.value[BD_BOOST] = rpm ? sample.pedal_tenths * rpm / 270000.0 : 0;
            frame.value[BD_RPM] = rpm;
            frame.value[BD_IAT] = 22.0 + sample.pedal_tenths / 35.0;
            frame.value[BD_COOLANT] = sample.coolant;
            frame.value[BD_OIL] = rpm ? 1.4 + rpm/2500.0 : 0;
            frame.value[BD_ETHANOL]=10;
            frame.valid |= (1u<<BD_AFR)|(1u<<BD_TIMING)|(1u<<BD_TRANS)|(1u<<BD_RAIL)|
                           (1u<<BD_SPEED)|(1u<<BD_OIL_TEMP)|(1u<<BD_LOAD)|(1u<<BD_MAF)|
                           (1u<<BD_MAP)|(1u<<BD_ACCELERATOR);
            frame.value[BD_AFR]=14.7;
            frame.value[BD_TIMING]=rpm ? 7+sample.pedal_tenths/100.0 : 0;
            frame.value[BD_TRANS]=sample.coolant*0.82;
            frame.value[BD_RAIL]=rpm ? 95+sample.pedal_tenths/8.0 : 0;
            frame.value[BD_SPEED]=sample.kph;
            frame.value[BD_OIL_TEMP]=sample.coolant*0.88;
            frame.value[BD_LOAD]=frame.value[BD_ACCELERATOR]=sample.pedal_tenths/10.0;
            frame.value[BD_MAF]=rpm/45.0;
            frame.value[BD_MAP]=100+frame.value[BD_BOOST]*6.89476;
            char packet[BD_MAX_PACKET];
            int length = bd_encode(&frame,packet,sizeof packet);
            if (length > 0) render(packet,unsigned(length),now);
        } else if (page_ == shift || page_ == launchPage) {
            const drive_frame frame = {sample.sampled_ms,rpm,sample.kph,sample.sampled_ms};
            render(reinterpret_cast<const char*>(&frame),sizeof frame,now);
            if (page_ == shift) {
                const char gear[] = {"PRND"[sample.gear],0};
                strcpy(last_[2],gear);
            }
        } else if (page_ == warm) {
            const warm_frame frame = {
                {sample.sampled_ms,sample.sampled_ms,sample.sampled_ms},
                {sample.coolant*0.88,double(sample.coolant),sample.coolant*0.82}
            };
            render(reinterpret_cast<const char*>(&frame),sizeof frame,now);
        } else if (page_ == road) {
            const int mode = sample.kph < 0 ? ROAD_JET_WAITING :
                sample.kph <= ROAD_JET_MAX_KPH ? ROAD_JET_ACTIVE : ROAD_JET_TOO_FAST;
            const road_jet_frame frame = {sample.sampled_ms,mode,sample.pedal_tenths,sample.steering_hundredths};
            updateRoad(frame,now);
        }
        lastPacket_ = sample.sampled_ms;
        received_ = true;
        if (page_ != road && page_ != boost && page_ != warm) status(page_,"IDEmu simulated telemetry");
    }
#endif
    const char* recordPath() const {
        static const char* const paths[] = {
            "/var/opt/hmi/data/ID5/ID6LibraryLab.0-60",
            "/var/opt/hmi/data/ID5/ID6LibraryLab.60-130",
            "/var/opt/hmi/data/ID5/ID6LibraryLab.0-100",
            "/var/opt/hmi/data/ID5/ID6LibraryLab.100-200"
        };
        return paths[launchMode_];
    }
    void resetLaunch() {
        lt_set_mode(&timer_,launchMode_);
        records_ = loadLaunchRecords(recordPath());
        static const char* const modeNames[] = {"0-60 mph","60-130 mph","0-100 km/h","100-200 km/h"};
        updateModel(615u,bmw::BasicString<char>(modeNames[launchMode_]));
    }
    bool nextLaunchMode(const bmw::Parcel&) {
        if (page_ != launchPage) return true;
        launchMode_ = static_cast<lt_mode>((unsigned(launchMode_)+1) % LT_MODE_COUNT);
        saveGaugePreferences(kLaunchPreferences,launchMode_,launchMode_ >= LT_0_100);
        clear(launchPage);
        resetLaunch();
        return true;
    }
    void showCodes() {
        const char* row = strchr(codeBuffer_, '\n');
        if (row) ++row;
        for (unsigned skip = 0; row && *row && skip < codeOffset_; ++skip) {
            row = strchr(row, '\n');
            if (row) ++row;
        }
        if (codeOffset_ && (!row || !*row)) {
            codeOffset_ = 0;
            showCodes();
            return;
        }
        for (unsigned i = 0; i < 4; ++i) {
            char line[36];
            size_t size = row ? strcspn(row,"\r\n") : 0;
            if (size >= sizeof line) size = sizeof line - 1;
            snprintf(line,sizeof line,"%.*s",int(size),row ? row : "");
            display(codes,i,line);
            if (row) { row = strchr(row,'\n'); if (row) ++row; }
        }
        int length = int(strcspn(codeBuffer_,"\r\n"));
        char heading[96];
        snprintf(heading,sizeof heading,"%.*s",length,codeBuffer_);
        status(codes,heading[0] ? heading : "No diagnostic result");
    }
    void startCodes(bool clear) {
        codesStarted_ = true;
        codeSize_ = codeOffset_ = 0;
        codeBuffer_[0] = 0;
#ifdef ID6_EMULATOR
        snprintf(codeBuffer_,sizeof codeBuffer_,"%s",clear ?
                 "IDEmu: Confirmed clear in the test fixture\n" :
                 "IDEmu: 3 sample engine codes\n100213  status 01\n120308  status 04\nFFFFFF  status 08\n");
        codeSize_=strlen(codeBuffer_);
        showCodes();
#else
        int channel[2];
        if (pipe(channel) < 0) { status(codes,"Cannot open diagnostics"); return; }
        char path[176], gatewayPath[176], gateway[16] = "auto";
        snprintf(path,sizeof path,"%s/engine-codes-qnx",kRoot);
        snprintf(gatewayPath,sizeof gatewayPath,"%s/gateway-ip.txt",kRoot);
        if (!readDiagnosticGateway(gatewayPath,gateway))
            readDiagnosticGateway(ID6_GATEWAY_SETTINGS,gateway);
        const char *args[] = {path,"--host",gateway,clear ? "--clear" : "--codes",0};
        if (fcntl(channel[0],F_SETFL,O_NONBLOCK) == 0 &&
            fcntl(channel[0],F_SETFD,FD_CLOEXEC) == 0 &&
            fcntl(channel[1],F_SETFD,FD_CLOEXEC) == 0)
            child_ = launch(args,channel[1]);
        close(channel[1]);
        if (child_ <= 0) {
            close(channel[0]);
            status(codes,"Cannot start diagnostics");
            return;
        }
        socket_ = channel[0];
        workerPage_ = codes;
        childStopping_ = false;
        status(codes,clear ? "Clearing engine fault memory" : "Reading engine fault memory");
#endif
    }
    void pollCodes() {
        if (!codesStarted_ && child_ < 0) {
            const bool clear=codesClearPending_;
            codesClearPending_=false;
            startCodes(clear);
        }
        if (socket_ < 0 || workerPage_ != codes) return;
        if (codeSize_ < sizeof codeBuffer_ - 1) {
            ssize_t count = read(socket_,codeBuffer_+codeSize_,sizeof codeBuffer_-codeSize_-1);
            if (count > 0) {
                codeSize_ += unsigned(count);
                codeBuffer_[codeSize_] = 0;
                return;
            }
            if (count < 0 && (errno == EINTR || errno == EAGAIN)) return;
        }
        close(socket_);
        socket_ = -1;
        showCodes();
    }
    bool nextCodes(const bmw::Parcel&) {
        if (page_ == codes && socket_ < 0 && codeSize_) {
            codeOffset_ += 4;
            showCodes();
        }
        return true;
    }
    bool confirmClearCodes(const bmw::Parcel&) {
        if (page_==codesConfirm) codesClearPending_=true;
        return true;
    }
    template <unsigned Page> bool preview(const bmw::Parcel& payload) {
        bool visible;
        // Previews track the highlighted tile while the Library itself owns focus.
        // They must never open pages, start a worker, or take over the display.
        if (!payload.readBool(visible) || !visible) return true;
        if (page_ == boost && Page == boostSettings) controlFocus_ = 214;
        else if (page_ == shift && (Page == shiftSettings || Page == shiftGasSettings)) controlFocus_ = 216;
        else if (page_ == updates && Page == updateConfirm) controlFocus_ = 260;
        else if (page_ == updates && Page == installConfirm) controlFocus_ = 261;
        else if (Page >= home && Page <= systemPage && Page != page_ &&
                 (page_ == updates || page_ == systemPage)) controlFocus_ = Page;
        return true;
    }

    template <unsigned Page> bool pageFocus(const bmw::Parcel& payload) {
        bool focused;
        if (payload.readBool(focused)) focusPage(Page,focused);
        return true;
    }

    void focusPage(unsigned Page,bool focused) {
        if (focused && page_ != Page) {
            stopWorker();
            page_ = Page;
            if (Page==boostSettings) refreshBoostSettings();
            if (Page==boostSettings || Page==warm)
                updateModel(1880u,bmw::BasicString<char>(dctTemperatureLabel()));
            if (Page==warm) {
                warmFahrenheit_=loadWarmFahrenheit();
                updateModel(1882u,bmw::BasicString<char>(warmUnitsLabel(warmFahrenheit_)));
            }
            if (Page==boost) for (unsigned i=0;i<3;++i) boostAlerts_.reset(i);
            switch (Page) {
                case boost: controlFocus_ = 214; break;
                case shift: controlFocus_ = 216; break;
                case mhd: controlFocus_ = 240; break;
                case beamer: controlFocus_ = 250; break;
                case codes: controlFocus_ = 230; break;
                case codesConfirm: controlFocus_ = 236; break;
                case launchPage: controlFocus_ = 231; break;
                case updates: controlFocus_ = systemPage; break;
                case systemPage: controlFocus_ = updates; break;
                default: controlFocus_ = 0; break;
            }
            lamps_ = 0;
            if (Page == road) {
                roadSample_ = roadDistance_ = roadAltitude_ = 0;
                roadLane_ = 2;
                roadMode_ = ROAD_JET_WAITING;
                roadHasScene_ = false;
            }
            if (Page == codes) codesStarted_ = false;
            surfaceFailed_ = false;
            telemetryFailed_ = false;
            lastPacket_ = 0;
            received_ = false;
            memset(last_,0,sizeof last_);
            surfaceStatus_[0]=0;
            if (usesWorker(Page)) {
                if (Page != road) clear(Page);
                if (Page == launchPage) resetLaunch();
                status(Page,"Connecting to diagnostics");
            } else if (Page == mhd) {
                showMhdSettings();
                status(mhd,"Select a setting");
            } else if (Page == beamer) status(beamer,"Select a lighting pattern");
            else if (usbPage(Page)) {
                status(Page,usbRead_ >= 0 ? "Checking the signed USB package..." :
                            Page == updates && usbReply_[0] ? usbReply_ : "");
            }
            else if (Page == codes) {
                for (unsigned i=0; i<4; ++i) {
                    memset(last_[i],0,sizeof last_[i]);
                    updateModel(1400u+20u*codes+i,bmw::BasicString<char>(""));
                }
                status(codes,"Waiting for diagnostics");
            }

            syncSurface();
            schedule();
        } else if (!focused && page_ == Page) {
            page_ = inactive;
            if (Page==boost && tone_>0) kill(tone_,SIGTERM);
            syncSurface();
            stopWorker();
            schedule();
        }
    }

    template<unsigned Page> void registerPage(unsigned state) {
        typedef bmw::Delegate<bool, const bmw::Parcel&> Action;
        Action action;
        action.m_obj = this;
        // RHMI previews are visible without being selected. Only the focused
        // state owns the fullscreen layer and live diagnostic worker.
        action.m_exec = &Action::execImpl<ID6LibraryLab, &ID6LibraryLab::pageFocus<Page> >;
        addHandler(state, 1u, action);
        action.m_exec = &Action::execImpl<ID6LibraryLab, &ID6LibraryLab::preview<Page> >;
        addHandler(state, 11u, action);
    }

    template<unsigned Target> bool saveShiftTarget(const bmw::Parcel&) {
        FullShiftPreferences next=shiftPrefs_;
        next.gasoline_target=Target;next.diesel=false;
        commitShiftSettings(next,true,true);
        return true;
    }

    void showShiftSettings(bool saved,bool units_change=false) {
        updateModel(1881u,bmw::BasicString<char>(!saved && units_change ? "Could not save speed units" :
                    shiftPrefs_.metric ? "Speed units: km/h" : "Speed units: mph"));
        char label[128];
        snprintf(label,sizeof label,"Engine profile: %s",shiftPrefs_.diesel ? "Diesel" : "Gasoline");
        updateModel(1883u,bmw::BasicString<char>(label));
        snprintf(label,sizeof label,"Shift light target: %u RPM",shiftPrefs_.targetRpm());
        updateModel(1884u,bmw::BasicString<char>(label));
        snprintf(label,sizeof label,"Diesel: target %u RPM, display %u RPM%s",shiftPrefs_.diesel_target,
                 4000+shiftPrefs_.diesel_scale*1000,shiftPrefs_.diesel ? "" : " (inactive)");
        updateModel(1885u,bmw::BasicString<char>(label));
        updateModel(1886u,bmw::BasicString<char>(saved ? "App choices; follow vehicle guidance" :
                    "Settings not saved; files may be partially updated"));
    }

    void commitShiftSettings(const FullShiftPreferences& next,bool gasoline,bool profile) {
        bool saved=true;
        if (gasoline) saved=next.saveGasoline(shiftPreferences_);
        if (profile) saved=next.saveProfile(shiftProfile_) && saved;
        if (saved) shiftPrefs_=next;
        shiftSaved_=saved;
        showShiftSettings(saved,gasoline && !profile);
        refreshShift(bd_mono_ms());
        syncSurface();
    }

    bool toggleShiftUnits(const bmw::Parcel&) {
        FullShiftPreferences next=shiftPrefs_;next.metric=!next.metric;
        commitShiftSettings(next,true,false);
        return true;
    }

    template<bool Diesel> bool setShiftProfile(const bmw::Parcel&) {
        FullShiftPreferences next=shiftPrefs_;next.diesel=Diesel;
        commitShiftSettings(next,false,true);return true;
    }
    template<bool Increase> bool adjustShiftTarget(const bmw::Parcel&) {
        FullShiftPreferences next=shiftPrefs_;next.adjustDiesel(Increase);
        commitShiftSettings(next,false,true);return true;
    }
    template<unsigned Scale> bool setShiftScale(const bmw::Parcel&) {
        FullShiftPreferences next=shiftPrefs_;next.setDieselScale(Scale);
        commitShiftSettings(next,false,true);return true;
    }

    void registerShiftMenu() {
        registerPage<shiftSettings>(216);
        registerPage<shiftGasSettings>(219);
        typedef bmw::Delegate<bool,const bmw::Parcel&> Action;
        typedef bool (*Handler)(void*,const bmw::Parcel&);
        Action action;
        action.m_obj = this;
#define TARGET(i) &Action::execImpl<ID6LibraryLab,&ID6LibraryLab::saveShiftTarget<i> >
        const Handler targets[] = {
            TARGET(0), TARGET(1), TARGET(2), TARGET(3), TARGET(4), TARGET(5), TARGET(6),
            TARGET(7), TARGET(8), TARGET(9), TARGET(10), TARGET(11), TARGET(12), TARGET(13),
            TARGET(14), TARGET(15), TARGET(16), TARGET(17), TARGET(18), TARGET(19), TARGET(20)
        };
#undef TARGET
        for (unsigned i=0; i<shift_target_count; ++i) {
            action.m_exec=targets[i];
            addHandler(5700+i,action);
        }
        action.m_exec=&Action::execImpl<ID6LibraryLab,&ID6LibraryLab::toggleShiftUnits>;
        addHandler(5751u,action);
        action.m_exec=&Action::execImpl<ID6LibraryLab,&ID6LibraryLab::setShiftProfile<false> >;addHandler(5800u,action);
        action.m_exec=&Action::execImpl<ID6LibraryLab,&ID6LibraryLab::setShiftProfile<true> >;addHandler(5801u,action);
        action.m_exec=&Action::execImpl<ID6LibraryLab,&ID6LibraryLab::setShiftScale<0> >;addHandler(5810u,action);
        action.m_exec=&Action::execImpl<ID6LibraryLab,&ID6LibraryLab::setShiftScale<1> >;addHandler(5811u,action);
        action.m_exec=&Action::execImpl<ID6LibraryLab,&ID6LibraryLab::setShiftScale<2> >;addHandler(5812u,action);
        action.m_exec=&Action::execImpl<ID6LibraryLab,&ID6LibraryLab::adjustShiftTarget<false> >;addHandler(5820u,action);
        action.m_exec=&Action::execImpl<ID6LibraryLab,&ID6LibraryLab::adjustShiftTarget<true> >;addHandler(5821u,action);
        showShiftSettings(true);
    }

    void registerBoostMenu() {
        registerPage<boostSettings>(214);
        registerPage<boostSettings>(215);
        registerPage<boostSettings>(2180);
        for (unsigned i=0;i<10;++i) registerPage<boostSettings>(2160+i);
        typedef bmw::Delegate<bool,const bmw::Parcel&> Action;
        typedef bool (*Handler)(void*,const bmw::Parcel&);
        Action action;
        action.m_obj=this;
#define G(index) &Action::execImpl<ID6LibraryLab,&ID6LibraryLab::boostPicker<index> >
        const Handler pickers[]={G(0),G(1),G(2),G(3),G(4),G(5),G(6),G(7),G(8)};
#undef G
        for (unsigned i=0;i<9;++i) {
            action.m_exec=pickers[i];
            addHandler(5580u+i,action);
        }
#define SIGNAL(id, name, lo, hi, unit, wide, narrow) &Action::execImpl<ID6LibraryLab,&ID6LibraryLab::boostGauge<BD_##id> >,
        const Handler signals[]={
#include "boost_signals.def"
        };
#undef SIGNAL
        for (unsigned i=0;i<BD_COUNT;++i) {
            action.m_exec=signals[i];
            addHandler(5500u+i,action);
        }
#define C(row, n) &Action::execImpl<ID6LibraryLab,&ID6LibraryLab::boostOption<row,n> >
#define TWO(row) C(row,0),C(row,1)
#define SIX(row) TWO(row),C(row,2),C(row,3),C(row,4),C(row,5)
#define NINE(row) SIX(row),C(row,6),C(row,7),C(row,8)
#define TEN(row) NINE(row),C(row,9)
        const Handler settings[][10]={
            {TWO(0),C(0,2)},{TWO(1)},{TWO(2)},{TWO(3)},{TWO(4)},
            {TEN(5)},{TEN(6)},{SIX(7)},{NINE(8)},{TWO(9)}
        };
#undef TEN
#undef NINE
#undef SIX
#undef TWO
#undef C
        for (unsigned row=0;row<10;++row)
            for (unsigned i=0;i<10 && settings[row][i];++i) {
                action.m_exec=settings[row][i];
                addHandler(5000u+row*16+i,action);
            }
    }

public:
    ID6LibraryLab(bmw::IDispatcher& dispatcher, bmw::BasicString<char> name,
                  unsigned major, unsigned minor, bmw::RHMIInterfaceVersion version,
                  const bmw::Bundle& cfg, const bmw::Bundle& res,
                  const char* shift_preferences=kShiftPreferences,const char* shift_profile=kShiftProfile)
        : bmw::PluginApp(dispatcher, name, major, minor, version, cfg, res),
          page_(inactive), workerPage_(inactive), socket_(-1), child_(-1),
          queued_(false), childStopping_(false), received_(false), telemetryFailed_(false), lastPacket_(0),
  #ifdef ID6_EMULATOR
          boostFiles_(0),
#endif
          boostSlot_(9), tone_(-1), lastTone_(0),
          shiftPreferences_(shift_preferences), shiftProfile_(shift_profile), shiftSaved_(true), warmFahrenheit_(false), launchMode_(LT_0_100), surfaceFailed_(false), codesStarted_(false),
          codeSize_(0), codeOffset_(0), controlFocus_(0), lamps_(0), map_(0),
          controlUsed_(0), cooling_(-1), usbChild_(-1), usbRead_(-1), usbUsed_(0), roadSample_(0), roadMode_(ROAD_JET_WAITING),
          roadDistance_(0), roadLane_(2), roadAltitude_(0), roadHasScene_(false), codesClearPending_(false) {
        memset(last_,0,sizeof last_);
        surfaceStatus_[0]=0;
        usbReply_[0] = 0;
        memset(boostLevels_,0,sizeof boostLevels_);
        lt_init(&timer_);
    }
    virtual ~ID6LibraryLab() { stopWorker(); if (usbRead_ >= 0) close(usbRead_); }
    virtual bool onInit() {
        if (kEntryPage == updates) {
            registerPage<updates>(210);
            registerPage<systemPage>(211);
            registerPage<updateConfirm>(212);
            registerPage<installConfirm>(213);
            registerSelection<updates,systemPage>();
            registerSelection<systemPage,updates>();
            registerSelection<updates,260>();
            registerSelection<updates,261>();
            registerControl<updateConfirm,260,&ID6LibraryLab::usbAction<260> >();
            registerControl<installConfirm,261,&ID6LibraryLab::usbAction<261> >();
        } else {
            registerPage<kEntryPage>(200 + kEntryPage);
        }
        if (kEntryPage == boost) {
            loadBoostConfiguration();
            registerBoostMenu();
            registerSelection<boost,214>();
        }
        if (kEntryPage==boost || kEntryPage==warm) {
            typedef bmw::Delegate<bool,const bmw::Parcel&> Action;
            Action action;
            action.m_obj=this;
            action.m_exec=&Action::execImpl<ID6LibraryLab,&ID6LibraryLab::changeDctTemperature>;
            addHandler(5750u,action);
            updateModel(1880u,bmw::BasicString<char>(dctTemperatureLabel()));
            if (kEntryPage==warm) {
                warmFahrenheit_=loadWarmFahrenheit();
                action.m_exec=&Action::execImpl<ID6LibraryLab,&ID6LibraryLab::toggleWarmUnits>;
                addHandler(5752u,action);
                updateModel(1882u,bmw::BasicString<char>(warmUnitsLabel(warmFahrenheit_)));
            }
        }
        if (kEntryPage == shift) {
            shiftPrefs_.load(shiftPreferences_,shiftProfile_);
            registerShiftMenu();
            registerSelection<shift,216>();
        }
        if (kEntryPage == launchPage) {
            unsigned mode;
            bool metric;
            if (loadGaugePreferences(kLaunchPreferences,LT_MODE_COUNT,mode,metric))
                launchMode_ = static_cast<lt_mode>(mode);
            registerControl<launchPage,231,&ID6LibraryLab::nextLaunchMode>();
        }
        if (kEntryPage == codes) {
            registerControl<codes,230,&ID6LibraryLab::nextCodes>();
            registerSelection<codes,232>();
            registerPage<codesConfirm>(217);
            registerSelection<codesConfirm,234>();
            registerSelection<codesConfirm,236>();
            typedef bmw::Delegate<bool,const bmw::Parcel&> Action;
            Action action;
            action.m_obj=this;
            action.m_exec=&Action::execImpl<ID6LibraryLab,&ID6LibraryLab::confirmClearCodes>;
            addHandler(234u,action);
        }
        if (kEntryPage == mhd) {
            registerControl<mhd,240,&ID6LibraryLab::mhdAction<240> >();
            registerControl<mhd,241,&ID6LibraryLab::mhdAction<241> >();
            registerControl<mhd,242,&ID6LibraryLab::mhdAction<242> >();
            registerControl<mhd,243,&ID6LibraryLab::mhdAction<243> >();
            registerControl<mhd,244,&ID6LibraryLab::mhdAction<244> >();
            registerControl<mhd,245,&ID6LibraryLab::mhdAction<245> >();
            registerControl<mhd,246,&ID6LibraryLab::mhdAction<246> >();
            registerControl<mhd,247,&ID6LibraryLab::mhdAction<247> >();
            registerControl<mhd,248,&ID6LibraryLab::mhdAction<248> >();
        }
        if (kEntryPage == beamer) {
            registerControl<beamer,250,&ID6LibraryLab::beamAction<250> >();
            registerControl<beamer,251,&ID6LibraryLab::beamAction<251> >();
            registerControl<beamer,252,&ID6LibraryLab::beamAction<252> >();
            registerControl<beamer,253,&ID6LibraryLab::beamAction<253> >();
            registerControl<beamer,254,&ID6LibraryLab::beamAction<254> >();
        }
        return true;
    }
    virtual bool onFinalize() { page_=inactive; syncSurface(); surface_.closeChannel(); stopWorker(); return true; }
    virtual bool invokeDataUpdate(unsigned key, const bmw::Parcel& data) {
        if (key != poll_key) return bmw::PluginApp::invokeDataUpdate(key, data);
        queued_ = false;
        surface_.reap();
        pollUsb();
        reapTone();
#ifdef ID6_EMULATOR
        if (kEntryPage == boost && boostFiles_ != 3) {
            const unsigned found=(access(kBoostPrefs,F_OK)==0 ? 1u : 0u) |
                                 (access(kBoostLayout,F_OK)==0 ? 2u : 0u);
            if (boostFiles_!=found) {
                boostFiles_=found;
                loadBoostConfiguration();
                refreshBoostSettings();
            }
        }
#endif
        const bool wasRunning = child_ > 0 && !childStopping_;
        reap();
        if (page_ == inactive) { if (child_ > 0 || usbChild_ > 0 || usbRead_ >= 0 || tone_>0) schedule(); return true; }
#ifdef ID6_EMULATOR
        if (usesWorker(page_)) { pollEmulator(); finishPoll(); return true; }
#endif
        if (page_ == codes) { pollCodes(); finishPoll(); return true; }
        if (page_ == mhd || page_ == beamer) { pollControl(); finishPoll(); return true; }
        if (!usesWorker(page_) || telemetryFailed_) { finishPoll(); return true; }
        if (child_ < 0 && wasRunning) {
            stopWorker();
            telemetryFailed_ = true;
            status(page_, "Worker stopped. Reopen the page to retry.");
            if (page_ == road) roadMode_ = ROAD_JET_WAITING;
            else clear(page_);
            finishPoll();
            return true;
        }
        if (child_ < 0 && !startWorker()) {
            status(page_, "Cannot start local telemetry.");
            telemetryFailed_ = true;
            finishPoll();
            return true;
        }
        if (socket_ >= 0 && workerPage_ == page_) {
            char packet[BD_MAX_PACKET];
            const uint64_t now = bd_mono_ms();
            for (unsigned i = 0; i < 8; ++i) {
                struct sockaddr_in sender;
                socklen_t senderSize=sizeof sender;
                const ssize_t got = recvfrom(socket_, packet, sizeof packet, 0,
                    reinterpret_cast<struct sockaddr*>(&sender),&senderSize);
                if (got <= 0) break;
                if (page_==warm && (childStopping_ || senderSize!=sizeof sender ||
                    sender.sin_family!=AF_INET || sender.sin_addr.s_addr!=htonl(INADDR_LOOPBACK) ||
                    ntohs(sender.sin_port)!=WARM_ACTIVE_PORT)) continue;
                if (page_==shift && (childStopping_ || senderSize!=sizeof sender ||
                    sender.sin_family!=AF_INET || sender.sin_addr.s_addr!=htonl(INADDR_LOOPBACK) ||
                    ntohs(sender.sin_port)!=SHIFT_ACTIVE_PORT)) continue;
                render(packet, static_cast<unsigned>(got), now);
            }
            if (page_ == road) {
                if (!fresh(now,lastPacket_,ROAD_JET_STALE_MS)) setRoadMode(ROAD_JET_WAITING);
                finishPoll();
                return true;
            }
            if (!fresh(now, lastPacket_, page_ == warm ? unsigned(WARM_STALE_MS) : unsigned(BD_STALE_MS))) {
                if (received_) {
                    clear(page_);
                    if (page_ == launchPage) lt_set_mode(&timer_,launchMode_);
                    received_ = false;
                }
                status(page_, "Waiting for valid telemetry");
                lastPacket_ = now;
            }
        }
        finishPoll();
        return true;
    }
};

extern "C" {
PluginAppInfo pluginAppInfo = {1, ID6_PLUGIN_NAME, "local-development", 1,
                              create_app<ID6LibraryLab>, destroy_app};
}
