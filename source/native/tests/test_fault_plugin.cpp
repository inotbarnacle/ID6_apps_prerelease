#include "fake_bmw.h"
#include <assert.h>
#include <poll.h>
#include <sys/stat.h>

namespace bmw { class IDispatcher {}; class Bundle {}; }
extern "C" uint64_t bd_mono_ms(void) { return 10000; }
#include "../src/plugins/engine_codes.cpp"

static std::string last(unsigned model) {
    for (std::vector<FakeUpdate>::reverse_iterator it=fake_updates.rbegin();it!=fake_updates.rend();++it)
        if (it->model==model && it->kind=='s') return it->text;
    assert(!"model not updated");
    return "";
}
static void action(unsigned id) {
    bmw::Parcel event;
    assert(fake_fire(0,id,event));
}
static void result(EngineCodes& app, const char* text) {
    assert(strlen(text)<sizeof app.code_result_);
    strcpy(app.code_result_,text);
    app.code_size_=strlen(text);
    app.code_page_=0;
    app.renderCodes();
}
static void finish(EngineCodes& app) {
    for (unsigned i=0;i<1000 && (app.codes_fd_>=0 || app.codes_pid_>0);++i) {
        app.pollCodes();
        poll(0,0,5);
    }
    assert(app.codes_fd_<0 && app.codes_pid_<=0);
}
static std::string read_file(const char* path) {
    FILE* file=fopen(path,"rb");
    assert(file);
    char data[1024];
    size_t n=fread(data,1,sizeof data-1,file);
    assert(!ferror(file));
    fclose(file);
    data[n]=0;
    return data;
}

int main() {
    char directory[]="/tmp/fault-plugin.XXXXXX";
    assert(mkdtemp(directory));
    char prefs[256],worker[256],args[256];
    snprintf(prefs,sizeof prefs,"%s/preferences",directory);
    snprintf(worker,sizeof worker,"%s/worker",directory);
    snprintf(args,sizeof args,"%s/args",directory);
    FILE* script=fopen(worker,"wb");
    assert(script);
    // The worker is a local argv echo only; it never connects to diagnostics.
    fprintf(script,"#!/bin/sh\nprintf '%%s\\n' \"$@\" > '%s'\nprintf 'Engine: 1 code\\n02FF12  status 09 mask 08\\n'\n",args);
    assert(fclose(script)==0 && chmod(worker,0700)==0);
    bmw::IDispatcher dispatcher;
    bmw::Bundle bundle;
    bmw::RHMIInterfaceVersion version={1};
    fake_reset();
    {
        EngineCodes app(dispatcher,bmw::BasicString<char>("EngineCodes"),1,0,version,bundle,bundle,worker,prefs);
        assert(app.onInit());
        assert(last(80).find("gas ref")!=std::string::npos);
        result(app,"Engine: 2 codes\n02FF12  status 09 mask 08\n000001  status 20 mask 20\n");
        assert(last(81)=="02FF12 Confirmed" && last(82)=="000001 Since clear");
        assert(last(91)==kDmeDescriptions[0].text); // same existing reference lookup
        action(30);
        assert(app.prefs_.raw && last(80).find("DDE/raw")!=std::string::npos);
        assert(last(91).find("Description unavailable")!=std::string::npos);
        assert(last(91).find("09 / support 08")!=std::string::npos);
        assert(last(91).find(kDmeDescriptions[0].text)==std::string::npos);
        assert(access(args,F_OK)!=0); // changing a description profile never launches diagnostics
        result(app,"Engine: 1 code\n02FF12  status 01\n");
        assert(last(81)=="02FF12 Raw" && last(91).find("support unknown")!=std::string::npos);
        result(app,"Engine: 1 code\n-00001  status 01 mask FF\n");
        assert(last(81).find("Active")==std::string::npos && last(91).empty());
        result(app,"Read rejected (NRC 31)\n");
        assert(last(80).find("Read rejected")!=std::string::npos && last(81).empty());
        action(23);
        finish(app);
        std::string read_args=read_file(args);
        assert(read_args.find("--codes\n--status-mask\n")!=std::string::npos);
        assert(read_args.find("--clear")==std::string::npos && read_args.find("--confirm-clear")==std::string::npos);
        assert(last(81)=="02FF12 Confirmed");
        // The clear-menu navigation is an HMI action, not a worker callback.
        bmw::Parcel event;
        assert(!fake_fire(0,25,event));
        assert(read_file(args)==read_args);
        action(27); // explicit confirmation callback, with backend confirmation too
        finish(app);
        assert(read_file(args).find("--clear\n--status-mask\n--confirm-clear\n")!=std::string::npos);
        assert(app.onFinalize());
    }
    fake_reset();
    {
        EngineCodes app(dispatcher,bmw::BasicString<char>("EngineCodes"),1,0,version,bundle,bundle,worker,prefs);
        assert(app.onInit() && app.prefs_.raw);
        action(31);
        assert(!app.prefs_.raw && last(96).find("gasoline reference")!=std::string::npos);
        assert(app.onFinalize());
    }
    fake_reset();
    {
        EngineCodes app(dispatcher,bmw::BasicString<char>("EngineCodes"),1,0,version,bundle,bundle,
                        worker,"/nonexistent-idrive-directory/preferences");
        assert(app.onInit());
        action(30);
        assert(last(96).find("not saved")!=std::string::npos);
        assert(app.onFinalize());
    }
    unlink(prefs); unlink(worker); unlink(args); rmdir(directory);
    puts("EngineCodes production masked status, DDE/raw/reference profiles, read-only actions and clear confirmation passed.");
}
